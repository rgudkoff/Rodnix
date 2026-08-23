# Reads a kernel configuration (config/MASTER[.<arch>]) and prints the set
# of enabled options, one per line.
#
# Line format, after XNU's config/MASTER:
#
#   options   <NAME>          # what it turns on
#
# The name is what a manifest line says after "optional", and what reaches
# the compiler as -D<NAME>. Commenting a line out is how an option is
# turned off; there is no "no<NAME>" form, so the file always reads as the
# list of what this kernel is.

{
	sub(/#.*/, "")
	if (NF == 0)
		next

	if ($1 == "options" || $1 == "option") {
		if (NF != 2) {
			printf("%s:%d: 'options' takes exactly one name\n",
			       FILENAME, FNR) > "/dev/stderr"
			print "__CONF_ERROR__"
			status = 1
			next
		}
		print $2
		next
	}

	printf("%s:%d: expected 'options', found '%s'\n",
	       FILENAME, FNR, $1) > "/dev/stderr"
	print "__CONF_ERROR__"
	status = 1
}

END { exit status }
