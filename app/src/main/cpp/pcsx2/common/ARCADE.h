/**
 * @file ARCADE.h
 * @author El_isra
 * @brief Common definitions for Namco System 246/256 arcade hardware.
 */

#pragma once

enum ACMEDIATYPE
{
	ACUNK = -1,
	ACCD = 0,
	ACDVD,
	ACHDD,
};

#define ACMEDIATYPE_FROM_STRING(s) \
	((s) == "CD" ? ACMEDIATYPE::ACCD : \
		(s) == "DVD" ? ACMEDIATYPE::ACDVD : \
		(s) == "HDD" ? ACMEDIATYPE::ACHDD : \
		ACMEDIATYPE::ACUNK)
