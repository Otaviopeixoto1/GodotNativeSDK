import sys


def print_error(message):
    sys.stderr.write("ERROR: {}\n".format(message))


def print_warning(message):
    sys.stderr.write("WARNING: {}\n".format(message))
