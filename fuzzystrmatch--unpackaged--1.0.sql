/* contrib/fuzzystrmatch/fuzzystrmatch--unpackaged--1.1.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION fuzzystrmatch" to load this file. \quit

ALTER EXTENSION fuzzystrmatch ADD function levenshtein(text,text);
ALTER EXTENSION fuzzystrmatch ADD function levenshtein(text,text,integer,integer,integer);

ALTER EXTENSION fuzzystrmatch ADD function dameraulevenshteinnoncompatible(text,text,integer,integer,integer,integer);
ALTER EXTENSION fuzzystrmatch ADD function dameraulevenshtein(text,text);
ALTER EXTENSION fuzzystrmatch ADD function dameraulevenshtein(text,text,integer,integer,integer,integer);
ALTER EXTENSION fuzzystrmatch ADD function metaphone(text,integer);
ALTER EXTENSION fuzzystrmatch ADD function soundex(text);
ALTER EXTENSION fuzzystrmatch ADD function text_soundex(text);
ALTER EXTENSION fuzzystrmatch ADD function difference(text,text);
ALTER EXTENSION fuzzystrmatch ADD function dmetaphone(text);
ALTER EXTENSION fuzzystrmatch ADD function dmetaphone_alt(text);

-- these functions were not in 9.0

CREATE FUNCTION levenshtein_less_equal (text,text,int) RETURNS int
AS 'MODULE_PATHNAME','levenshtein_less_equal'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION levenshtein_less_equal (text,text,int,int,int,int) RETURNS int
AS 'MODULE_PATHNAME','levenshtein_less_equal_with_costs'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION dameraulevenshtein_less_equal (text,text,int) RETURNS int
AS 'MODULE_PATHNAME','dameraulevenshtein_less_equal'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION dameraulevenshtein_less_equal (text,text,int,int,int,int,int) RETURNS int
AS 'MODULE_PATHNAME','dameraulevenshtein_less_equal_with_costs'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION dameraulevenshtein_less_equal_noncompatible (text,text,int,int,int,int,int) RETURNS int
AS 'MODULE_PATHNAME','dameraulevenshtein_less_equal_with_costs_noncompatible'
LANGUAGE C IMMUTABLE STRICT;
