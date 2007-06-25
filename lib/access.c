/*
    access.c - Part of libsensors, a Linux library for reading sensor data.
    Copyright (c) 1998, 1999  Frodo Looijaard <frodol@dds.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <regex.h>
#include "access.h"
#include "sensors.h"
#include "data.h"
#include "error.h"
#include "proc.h"
#include "general.h"

#define GET_TYPE_REGEX "\\([[:alpha:]]\\{1,\\}\\)[[:digit:]]\\{0,\\}\\(_\\([[:alpha:]]\\{1,\\}\\)\\)\\{0,1\\}"

#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)
#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (const char *)__mptr - offsetof(type,member) );})

static int sensors_do_this_chip_sets(sensors_chip_name name);

/* Compare two chips name descriptions, to see whether they could match.
   Return 0 if it does not match, return 1 if it does match. */
int sensors_match_chip(sensors_chip_name chip1, sensors_chip_name chip2)
{
	if ((chip1.prefix != SENSORS_CHIP_NAME_PREFIX_ANY) &&
	    (chip2.prefix != SENSORS_CHIP_NAME_PREFIX_ANY) &&
	    strcasecmp(chip1.prefix, chip2.prefix))
		return 0;

	if ((chip1.bus != SENSORS_CHIP_NAME_BUS_ANY) &&
	    (chip2.bus != SENSORS_CHIP_NAME_BUS_ANY) &&
	    (chip1.bus != chip2.bus)) {

		if ((chip1.bus == SENSORS_CHIP_NAME_BUS_ISA) ||
		    (chip2.bus == SENSORS_CHIP_NAME_BUS_ISA))
			return 0;

		if ((chip1.bus == SENSORS_CHIP_NAME_BUS_PCI) ||
		    (chip2.bus == SENSORS_CHIP_NAME_BUS_PCI))
			return 0;

		if ((chip1.bus != SENSORS_CHIP_NAME_BUS_ANY_I2C) &&
		    (chip2.bus != SENSORS_CHIP_NAME_BUS_ANY_I2C))
			return 0;
	}

	if ((chip1.addr != chip2.addr) &&
	    (chip1.addr != SENSORS_CHIP_NAME_ADDR_ANY) &&
	    (chip2.addr != SENSORS_CHIP_NAME_ADDR_ANY))
		return 0;

	return 1;
}

/* Returns, one by one, a pointer to all sensor_chip structs of the
   config file which match with the given chip name. Last should be
   the value returned by the last call, or NULL if this is the first
   call. Returns NULL if no more matches are found. Do not modify
   the struct the return value points to! 
   Note that this visits the list of chips from last to first. Usually,
   you want the match that was latest in the config file. */
sensors_chip *sensors_for_all_config_chips(sensors_chip_name chip_name,
					   const sensors_chip * last)
{
	int nr, i;
	sensors_chip_name_list chips;

	for (nr = last ? last - sensors_config_chips - 1 :
			 sensors_config_chips_count - 1; nr >= 0; nr--) {

		chips = sensors_config_chips[nr].chips;
		for (i = 0; i < chips.fits_count; i++) {
			if (sensors_match_chip(chips.fits[i], chip_name))
				return sensors_config_chips + nr;
		}
	}
	return NULL;
}

/* Look up a resource in the intern chip list, and return a pointer to it. 
   Do not modify the struct the return value points to! Returns NULL if 
   not found.*/
const sensors_chip_feature *sensors_lookup_feature_nr(const char *prefix,
						      int feature)
{
	int i, j;
	const sensors_chip_feature *features;

	for (i = 0; sensors_chip_features_list[i].prefix; i++)
		if (!strcasecmp(sensors_chip_features_list[i].prefix, prefix)) {
			features = sensors_chip_features_list[i].feature;
			for (j = 0; features[j].data.name; j++)
				if (features[j].data.number == feature)
					return features + j;
		}
	return NULL;
}

/* Look up a resource in the intern chip list, and return a pointer to it. 
   Do not modify the struct the return value points to! Returns NULL if 
   not found.*/
const sensors_chip_feature *sensors_lookup_feature_name(const char *prefix,
							const char *feature)
{
	int i, j;
	const sensors_chip_feature *features;

	for (i = 0; sensors_chip_features_list[i].prefix; i++)
		if (!strcasecmp(sensors_chip_features_list[i].prefix, prefix)) {
			features = sensors_chip_features_list[i].feature;
			for (j = 0; features[j].data.name; j++)
				if (!strcasecmp(features[j].data.name, feature))
					return features + j;
		}
	return NULL;
}

/* Check whether the chip name is an 'absolute' name, which can only match
   one chip, or whether it has wildcards. Returns 0 if it is absolute, 1
   if there are wildcards. */
int sensors_chip_name_has_wildcards(sensors_chip_name chip)
{
	if ((chip.prefix == SENSORS_CHIP_NAME_PREFIX_ANY) ||
	    (chip.bus == SENSORS_CHIP_NAME_BUS_ANY) ||
	    (chip.bus == SENSORS_CHIP_NAME_BUS_ANY_I2C) ||
	    (chip.addr == SENSORS_CHIP_NAME_ADDR_ANY))
		return 1;
	else
		return 0;
}

/* Look up the label which belongs to this chip. Note that chip should not
   contain wildcard values! *result is newly allocated (free it yourself).
   This function will return 0 on success, and <0 on failure.
   If no label exists for this feature, its name is returned itself. */
int sensors_get_label(sensors_chip_name name, int feature, char **result)
{
	const sensors_chip *chip;
	const sensors_chip_feature *featureptr;
	int i;

	*result = NULL;
	if (sensors_chip_name_has_wildcards(name))
		return -SENSORS_ERR_WILDCARDS;
	if (!(featureptr = sensors_lookup_feature_nr(name.prefix, feature)))
		return -SENSORS_ERR_NO_ENTRY;

	for (chip = NULL; (chip = sensors_for_all_config_chips(name, chip));)
		for (i = 0; i < chip->labels_count; i++)
			if (!strcasecmp(featureptr->data.name,chip->labels[i].name)){
				if (*result)
					free(*result);
				if (!(*result = strdup(chip->labels[i].value)))
					sensors_fatal_error("sensors_get_label",
							    "Allocating label text");
				return 0;
			}

	/* No label, return the feature name instead */
	if (!(*result = strdup(featureptr->data.name)))
		sensors_fatal_error("sensors_get_label",
				    "Allocating label text");
	return 0;
}

int sensors_get_ignored(sensors_chip_name name, int feature)
{
	const sensors_chip *chip;
	const sensors_chip_feature *featureptr;
	const sensors_chip_feature *alt_featureptr;
	int i, res;

	/* Default: valid */
	res = 1;
	if (sensors_chip_name_has_wildcards(name))
		return -SENSORS_ERR_WILDCARDS;
	if (!(featureptr = sensors_lookup_feature_nr(name.prefix, feature)))
		return -SENSORS_ERR_NO_ENTRY;
	if (featureptr->data.mapping == SENSORS_NO_MAPPING)
		alt_featureptr = NULL;
	else if (!(alt_featureptr =
		   sensors_lookup_feature_nr(name.prefix,
					     featureptr->data.mapping)))
		return -SENSORS_ERR_NO_ENTRY;
	for (chip = NULL; (chip = sensors_for_all_config_chips(name, chip));)
		for (i = 0; i < chip->ignores_count; i++)
			if (!strcasecmp(featureptr->data.name, chip->ignores[i].name))
				return 0; /* Exact match always overrules! */
			else if (alt_featureptr &&
				 !strcasecmp(alt_featureptr->data.name,
					     chip->ignores[i].name))
				res = 0;
	return res;
}

/* Read the value of a feature of a certain chip. Note that chip should not
   contain wildcard values! This function will return 0 on success, and <0
   on failure. */
int sensors_get_feature(sensors_chip_name name, int feature, double *result)
{
	const sensors_chip_feature *main_feature;
	const sensors_chip_feature *alt_feature;
	const sensors_chip *chip;
	const sensors_expr *expr = NULL;
	double val;
	int res, i;
	int final_expr = 0;

	if (sensors_chip_name_has_wildcards(name))
		return -SENSORS_ERR_WILDCARDS;
	if (!(main_feature = sensors_lookup_feature_nr(name.prefix, feature)))
		return -SENSORS_ERR_NO_ENTRY;
	if (main_feature->data.compute_mapping == SENSORS_NO_MAPPING)
		alt_feature = NULL;
	else if (!(alt_feature = sensors_lookup_feature_nr(name.prefix,
					main_feature->data.compute_mapping)))
		return -SENSORS_ERR_NO_ENTRY;
	if (!(main_feature->data.mode & SENSORS_MODE_R))
		return -SENSORS_ERR_ACCESS_R;
	for (chip = NULL;
	     !expr && (chip = sensors_for_all_config_chips(name, chip));)
		for (i = 0; !final_expr && (i < chip->computes_count); i++) {
			if (!strcasecmp(main_feature->data.name, chip->computes[i].name)) {
				expr = chip->computes[i].from_proc;
				final_expr = 1;
			} else if (alt_feature && !strcasecmp(alt_feature->data.name,
					       chip->computes[i].name)) {
				expr = chip->computes[i].from_proc;
			}
		}
	if (sensors_read_proc(name, feature, &val))
		return -SENSORS_ERR_PROC;
	if (!expr)
		*result = val;
	else if ((res = sensors_eval_expr(name, expr, val, result)))
		return res;
	return 0;
}

/* Set the value of a feature of a certain chip. Note that chip should not
   contain wildcard values! This function will return 0 on success, and <0
   on failure. */
int sensors_set_feature(sensors_chip_name name, int feature, double value)
{
	const sensors_chip_feature *main_feature;
	const sensors_chip_feature *alt_feature;
	const sensors_chip *chip;
	const sensors_expr *expr = NULL;
	int i, res;
	int final_expr = 0;
	double to_write;

	if (sensors_chip_name_has_wildcards(name))
		return -SENSORS_ERR_WILDCARDS;
	if (!(main_feature = sensors_lookup_feature_nr(name.prefix, feature)))
		return -SENSORS_ERR_NO_ENTRY;
	if (main_feature->data.compute_mapping == SENSORS_NO_MAPPING)
		alt_feature = NULL;
	else if (!(alt_feature = sensors_lookup_feature_nr(name.prefix,
					     main_feature->data.compute_mapping)))
		return -SENSORS_ERR_NO_ENTRY;
	if (!(main_feature->data.mode & SENSORS_MODE_W))
		return -SENSORS_ERR_ACCESS_W;
	for (chip = NULL;
	     !expr && (chip = sensors_for_all_config_chips(name, chip));)
		for (i = 0; !final_expr && (i < chip->computes_count); i++)
			if (!strcasecmp(main_feature->data.name, chip->computes[i].name)) {
				expr = chip->computes->to_proc;
				final_expr = 1;
			} else if (alt_feature && !strcasecmp(alt_feature->data.name,
					       chip->computes[i].name)) {
				expr = chip->computes[i].to_proc;
			}

	to_write = value;
	if (expr)
		if ((res = sensors_eval_expr(name, expr, value, &to_write)))
			return res;
	if (sensors_write_proc(name, feature, to_write))
		return -SENSORS_ERR_PROC;
	return 0;
}

const sensors_chip_name *sensors_get_detected_chips(int *nr)
{
	const sensors_chip_name *res;
	res = (*nr >= sensors_proc_chips_count ?
			NULL : &sensors_proc_chips[*nr].name);
	(*nr)++;
	return res;
}

const char *sensors_get_adapter_name(int bus_nr)
{
	int i;

	if (bus_nr == SENSORS_CHIP_NAME_BUS_ISA)
		return "ISA adapter";
	if (bus_nr == SENSORS_CHIP_NAME_BUS_PCI)
		return "PCI adapter";
	if (bus_nr == SENSORS_CHIP_NAME_BUS_DUMMY)
		return "Dummy adapter";
	for (i = 0; i < sensors_proc_bus_count; i++)
		if (sensors_proc_bus[i].number == bus_nr)
			return sensors_proc_bus[i].adapter;
	return NULL;
}

/* nr1-1 is the last main feature found; nr2-1 is the last subfeature found */
const sensors_feature_data *sensors_get_all_features(sensors_chip_name name,
						     int *nr1, int *nr2)
{
	sensors_chip_feature *feature_list;
	int i;

	for (i = 0; sensors_chip_features_list[i].prefix; i++)
		if (!strcasecmp(sensors_chip_features_list[i].prefix, name.prefix)) {
			feature_list = sensors_chip_features_list[i].feature;
			if (!*nr1 && !*nr2) {	/* Return the first entry */
				if (!feature_list[0].data.name)	/* The list may be empty */
					return NULL;
				*nr1 = *nr2 = 1;
				return &feature_list->data;
			}
			for ((*nr2)++; feature_list[*nr2 - 1].data.name; (*nr2)++)
				if (feature_list[*nr2 - 1].data.mapping ==
				    feature_list[*nr1 - 1].data.number)
					return &((feature_list + *nr2 - 1)->data);
			for ((*nr1)++;
			     feature_list[*nr1 - 1].data.name
			     && (feature_list[*nr1 - 1].data.mapping !=
				 SENSORS_NO_MAPPING); (*nr1)++) ;
			*nr2 = *nr1;
			if (!feature_list[*nr1 - 1].data.name)
				return NULL;
			return &((feature_list + *nr1 - 1)->data);
		}
	return NULL;
}

int sensors_eval_expr(sensors_chip_name chipname, const sensors_expr * expr,
		      double val, double *result)
{
	double res1, res2;
	int res;
	const sensors_chip_feature *feature;

	if (expr->kind == sensors_kind_val) {
		*result = expr->data.val;
		return 0;
	}
	if (expr->kind == sensors_kind_source) {
		*result = val;
		return 0;
	}
	if (expr->kind == sensors_kind_var) {
		if (!(feature = sensors_lookup_feature_name(chipname.prefix,
							    expr->data.var)))
			return SENSORS_ERR_NO_ENTRY;
		if (!(res = sensors_get_feature(chipname, feature->data.number, result)))
			return res;
		return 0;
	}
	if ((res = sensors_eval_expr(chipname, expr->data.subexpr.sub1, val, &res1)))
		return res;
	if (expr->data.subexpr.sub2 &&
	    (res = sensors_eval_expr(chipname, expr->data.subexpr.sub2, val, &res2)))
		return res;
	switch (expr->data.subexpr.op) {
	case sensors_add:
		*result = res1 + res2;
		return 0;
	case sensors_sub:
		*result = res1 - res2;
		return 0;
	case sensors_multiply:
		*result = res1 * res2;
		return 0;
	case sensors_divide:
		if (res2 == 0.0)
			return -SENSORS_ERR_DIV_ZERO;
		*result = res1 / res2;
		return 0;
	case sensors_negate:
		*result = -res1;
		return 0;
	case sensors_exp:
		*result = exp(res1);
		return 0;
	case sensors_log:
		if (res1 < 0.0)
			return -SENSORS_ERR_DIV_ZERO;
		*result = log(res1);
		return 0;
	}
	return 0;
}

/* Execute all set statements for this particular chip. The chip may not 
   contain wildcards!  This function will return 0 on success, and <0 on 
   failure. */
int sensors_do_this_chip_sets(sensors_chip_name name)
{
	sensors_chip *chip;
	double value;
	int i, j;
	int err = 0, res;
	const sensors_chip_feature *feature;
	int *feature_list = NULL;
	int feature_count = 0;
	int feature_max = 0;
	int feature_nr;

	for (chip = NULL; (chip = sensors_for_all_config_chips(name, chip));)
		for (i = 0; i < chip->sets_count; i++) {
			feature = sensors_lookup_feature_name(name.prefix,
							chip->sets[i].name);
			if (!feature) {
				sensors_parse_error("Unknown feature name",
						    chip->sets[i].lineno);
				err = SENSORS_ERR_NO_ENTRY;
				continue;
			}
			feature_nr = feature->data.number;

			/* Check whether we already set this feature */
			for (j = 0; j < feature_count; j++)
				if (feature_list[j] == feature_nr)
					break;
			if (j != feature_count)
				continue;
			sensors_add_array_el(&feature_nr, &feature_list,
					     &feature_count, &feature_max,
					     sizeof(int));

			res = sensors_eval_expr(name, chip->sets[i].value, 0,
					      &value);
			if (res) {
				sensors_parse_error("Error parsing expression",
						    chip->sets[i].lineno);
				err = res;
				continue;
			}
			if ((res = sensors_set_feature(name, feature_nr, value))) {
				sensors_parse_error("Failed to set feature",
						chip->sets[i].lineno);
				err = res;
				continue;
			}
		}
	free(feature_list);
	return err;
}

/* Execute all set statements for this particular chip. The chip may contain
   wildcards!  This function will return 0 on success, and <0 on failure. */
int sensors_do_chip_sets(sensors_chip_name name)
{
	int nr, this_res;
	const sensors_chip_name *found_name;
	int res = 0;

	for (nr = 0; (found_name = sensors_get_detected_chips(&nr));)
		if (sensors_match_chip(name, *found_name)) {
			this_res = sensors_do_this_chip_sets(*found_name);
			if (!res)
				res = this_res;
		}
	return res;
}

/* Execute all set statements for all detected chips. This is the same as
   calling sensors_do_chip_sets with an all wildcards chip name */
int sensors_do_all_sets(void)
{
	sensors_chip_name name = { SENSORS_CHIP_NAME_PREFIX_ANY,
		SENSORS_CHIP_NAME_BUS_ANY,
		SENSORS_CHIP_NAME_ADDR_ANY
	};
	return sensors_do_chip_sets(name);
}

/* Static mappings for use by sensors_feature_get_type() */
struct feature_type_match
{
	const char *name;
	sensors_feature_type type;
	
	struct feature_type_match *submatches;
};

static struct feature_type_match temp_matches[] = {
	{ "max", SENSORS_FEATURE_TEMP_MAX },
	{ "max_hyst", SENSORS_FEATURE_TEMP_MAX_HYST },
	{ "min", SENSORS_FEATURE_TEMP_MIN },
	{ "crit", SENSORS_FEATURE_TEMP_CRIT },
	{ "crit_hyst", SENSORS_FEATURE_TEMP_CRIT_HYST },
	{ "alarm", SENSORS_FEATURE_TEMP_ALARM },
	{ "min_alarm", SENSORS_FEATURE_TEMP_MIN_ALARM },
	{ "max_alarm", SENSORS_FEATURE_TEMP_MAX_ALARM },
	{ "crit_alarm", SENSORS_FEATURE_TEMP_CRIT_ALARM },
	{ "fault", SENSORS_FEATURE_TEMP_FAULT },
	{ "type", SENSORS_FEATURE_TEMP_SENS },
	{ 0 }
};

static struct feature_type_match in_matches[] = {
	{ "min", SENSORS_FEATURE_IN_MIN },
	{ "max", SENSORS_FEATURE_IN_MAX },
	{ "alarm", SENSORS_FEATURE_IN_ALARM },
	{ "min_alarm", SENSORS_FEATURE_IN_MIN_ALARM },
	{ "max_alarm", SENSORS_FEATURE_IN_MAX_ALARM },
	{ 0 }
};

static struct feature_type_match fan_matches[] = {
	{ "min", SENSORS_FEATURE_FAN_MIN },
	{ "div", SENSORS_FEATURE_FAN_DIV },
	{ "alarm", SENSORS_FEATURE_FAN_ALARM },
	{ "fault", SENSORS_FEATURE_FAN_FAULT },
	{ 0 }
};

static struct feature_type_match matches[] = { 
	{ "temp", SENSORS_FEATURE_TEMP, temp_matches },
	{ "in", SENSORS_FEATURE_IN, in_matches },
	{ "fan", SENSORS_FEATURE_FAN, fan_matches },
	{ "vrm", SENSORS_FEATURE_VRM, 0 },
	{ "vid", SENSORS_FEATURE_VID, 0 },
	{ "sensor", SENSORS_FEATURE_TEMP_SENS, 0 }, 
	{ 0 }
};

/* Return the feature type based on the feature name */
sensors_feature_type sensors_feature_get_type(
	const sensors_feature_data *feature)
{
	const char *name;
	regmatch_t pmatch[4];
	int size_first, size_second, retval, i;
	struct feature_type_match *submatches;
	static regex_t reg;
	static regex_t *preg = NULL;
	
	/* use sysname if exists */
	if (container_of(feature, const struct sensors_chip_feature, data)->sysname)
		name = container_of(feature, const struct sensors_chip_feature, data)->sysname;
	else
		name = feature->name;
	
	if (!preg) {
		regcomp(&reg, GET_TYPE_REGEX, 0);
		preg = &reg;
	}
	
	retval = regexec(preg, name, 4, pmatch, 0);
	
	if (retval == -1)
		return SENSORS_FEATURE_UNKNOWN;
	
	size_first = pmatch[1].rm_eo - pmatch[1].rm_so;
	size_second = pmatch[3].rm_eo - pmatch[3].rm_so;
	
	for(i = 0; matches[i].name != 0; i++)
		if (!strncmp(name, matches[i].name, size_first))
			break;
	
	if (matches[i].name == NULL) /* no match */
		return SENSORS_FEATURE_UNKNOWN;
	else if (size_second == 0) /* single type */
		return matches[i].type;
	else if (matches[i].submatches == NULL) /* not single type, but no submatches */
		return SENSORS_FEATURE_UNKNOWN;

	submatches = matches[i].submatches;
	for(i = 0; submatches[i].name != 0; i++)
		if (!strcmp(name + pmatch[3].rm_so, submatches[i].name))
			return submatches[i].type;
	
	return SENSORS_FEATURE_UNKNOWN;
}
