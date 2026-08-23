# Reads a component source manifest (<component>/conf/files[.<arch>]) and
# prints the sources that are enabled for the current option set.
#
# Line format, after XNU's bsd/conf/files:
#
#   <path>            standard
#   <path>            optional <option> [<option> ...]
#
# "standard" is always built. "optional" is built only when every named
# option is set in config/MASTER[.<arch>]; the option names are matched
# case-insensitively, so a manifest can read the way the C macro does.
#
# Anything after '#' is a comment. Paths are relative to the source root,
# not to the manifest -- the same convention XNU uses, so a path can be
# grepped for exactly as it is written.

BEGIN {
	n = split(options, list, " ")
	for (i = 1; i <= n; i++)
		enabled[toupper(list[i])] = 1
	status = 0
}

{
	sub(/#.*/, "")
	if (NF == 0)
		next

	path = $1

	if ($2 == "standard") {
		if (NF != 2)
			bad("'standard' takes no arguments")
		else
			print path
		next
	}

	if ($2 == "optional") {
		if (NF < 3) {
			bad("'optional' needs at least one option name")
			next
		}
		for (i = 3; i <= NF; i++)
			if (!(toupper($i) in enabled))
				next
		print path
		next
	}

	bad("expected 'standard' or 'optional', found '" $2 "'")
}

function bad(msg) {
	printf("%s:%d: %s\n", FILENAME, FNR, msg) > "/dev/stderr"
	status = 1
	# Poison the output so make refuses to build a half-read manifest
	# rather than silently dropping the file this line named.
	print "__CONF_ERROR__"
}

END { exit status }
