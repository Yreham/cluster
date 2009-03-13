#!/usr/bin/python

#####
##
## The Following Agent Has Been Tested On:
##
##  Model                 Firmware
## +--------------------+---------------------------+
## (1) Main application	  BRET85K, rev 16  
##     Boot ROM           BRBR67D, rev 16
##     Remote Control     BRRG67D, rev 16
##
#####

import sys, re, pexpect
sys.path.append("@FENCEAGENTSLIBDIR@")
from fencing import *

#BEGIN_VERSION_GENERATION
RELEASE_VERSION="New Bladecenter Agent - test release on steroids"
REDHAT_COPYRIGHT=""
BUILD_DATE="March, 2008"
#END_VERSION_GENERATION

def get_power_status(conn, options):
	try:
		node_cmd = "system:blade\[" + options["-n"] + "\]>"

		conn.send("env -T system:blade[" + options["-n"] + "]\r\n")
		i = conn.log_expect(options, [ node_cmd, "system>" ] , SHELL_TIMEOUT)
		if i == 1:
			## Given blade number does not exist
			fail(EC_STATUS)
		conn.send("power -state\r\n")
		conn.log_expect(options, node_cmd, SHELL_TIMEOUT)
		status = conn.before.splitlines()[-1]
		conn.send("env -T system\r\n")
		conn.log_expect(options, options["-c"], SHELL_TIMEOUT)
	except pexpect.EOF:
		fail(EC_CONNECTION_LOST)
	except pexpect.TIMEOUT:
		fail(EC_TIMED_OUT)

	return status.lower().strip()

def set_power_status(conn, options):
	action = {
		'on' : "powerup",
		'off': "powerdown"
	}[options["-o"]]

	try:
		node_cmd = "system:blade\[" + options["-n"] + "\]>"

		conn.send("env -T system:blade[" + options["-n"] + "]\r\n")
		conn.log_expect(options, node_cmd, SHELL_TIMEOUT)
		conn.send("power -"+options["-o"]+"\r\n")
		conn.log_expect(options, node_cmd, SHELL_TIMEOUT)
		conn.send("env -T system\r\n")
		conn.log_expect(options, options["-c"], SHELL_TIMEOUT)
	except pexpect.EOF:
		fail(EC_CONNECTION_LOST)
	except pexpect.TIMEOUT:
		fail(EC_TIMED_OUT)

def get_blades_list(conn, options):
	outlets = { }
	try:
		node_cmd = "system>"

		conn.send("env -T system\r\n")
		conn.log_expect(options, node_cmd, SHELL_TIMEOUT)
		conn.send("list -l 2\r\n")
		conn.log_expect(options, node_cmd, SHELL_TIMEOUT)

		lines = conn.before.split("\r\n")
		filter_re = re.compile("^\s*blade\[(\d+)\]\s+(.*?)\s*$")
		for x in lines:
			res = filter_re.search(x)
			if res != None:
				outlets[res.group(1)] = (res.group(2), "")

	except pexpect.EOF:
		fail(EC_CONNECTION_LOST)
	except pexpect.TIMEOUT:
		fail(EC_TIMED_OUT)

	return outlets

def main():
	device_opt = [  "help", "version", "agent", "quiet", "verbose", "debug",
			"action", "ipaddr", "login", "passwd", "passwd_script",
			"cmd_prompt", "secure", "port", "identity_file", "separator" ]

	atexit.register(atexit_handler)

	options = check_input(device_opt, process_input(device_opt))

	## 
	## Fence agent specific defaults
	#####
	if 0 == options.has_key("-c"):
		options["-c"] = "system>"

	##
	## Operate the fencing device
	######
	conn = fence_login(options)
	fence_action(conn, options, set_power_status, get_power_status, get_blades_list)

	##
	## Logout from system
	######
	conn.send("exit\r\n")
	conn.close()

if __name__ == "__main__":
	main()
