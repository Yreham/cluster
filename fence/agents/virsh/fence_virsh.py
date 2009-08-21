#!/usr/bin/python

# The Following Agent Has Been Tested On:
#
# Virsh 0.3.3 on RHEL 5.2 with xen-3.0.3-51
#

import sys, re, pexpect, exceptions
sys.path.append("@FENCEAGENTSLIBDIR@")
from fencing import *

#BEGIN_VERSION_GENERATION
RELEASE_VERSION="Virsh fence agent"
REDHAT_COPYRIGHT=""
BUILD_DATE=""
#END_VERSION_GENERATION

def get_outlets_status(conn, options):
	try:
		conn.sendline("virsh list --all")
		conn.log_expect(options, options["-c"], SHELL_TIMEOUT)
	except pexpect.EOF:
		fail(EC_CONNECTION_LOST)
	except pexpect.TIMEOUT:
		fail(EC_TIMED_OUT)

	result={}

        #This is status of mini finite automata. 0 = we didn't found Id and Name, 1 = we did
        fa_status=0

        for line in conn.before.splitlines():
	        domain=re.search("^\s*(\S+)\s+(\S+)\s+(\S+).*$",line)

                if (domain!=None):
			if ((fa_status==0) and (domain.group(1).lower()=="id") and (domain.group(2).lower()=="name")):
				fa_status=1
			elif (fa_status==1):
				result[domain.group(2)]=("",(domain.group(3).lower() in ["running","blocked"] and "on" or "off"))
	return result

def get_power_status(conn, options):
	outlets=get_outlets_status(conn,options)

        if (not (options["-n"] in outlets)):
                fail_usage("Failed: You have to enter existing name of virtual machine!")
        else:
                return outlets[options["-n"]][1]

def set_power_status(conn, options):
	try:
		conn.sendline("virsh %s "%(options["-o"] == "on" and "start" or "destroy")+options["-n"])

		conn.log_expect(options, options["-c"], POWER_TIMEOUT)
                time.sleep(1)

	except pexpect.EOF:
		fail(EC_CONNECTION_LOST)
	except pexpect.TIMEOUT:
		fail(EC_TIMED_OUT)

def main():
	device_opt = [  "help", "version", "agent", "quiet", "verbose", "debug",
			"action", "ipaddr", "login", "passwd", "passwd_script",
			"secure", "identity_file", "test", "port", "separator",
			"inet4_only", "inet6_only", "ipport" ]

	atexit.register(atexit_handler)

	pinput = process_input(device_opt)
	pinput["-x"] = 1
	options = check_input(device_opt, pinput)

	## Defaults for fence agent
	if 0 == options.has_key("-c"):
		options["-c"] = "\[EXPECT\]#\ "

	options["ssh_options"]="-t '/bin/bash -c \"PS1=\[EXPECT\]#\  /bin/bash --noprofile --norc\"'"

	show_docs(options)

	## Operate the fencing device
	conn = fence_login(options)
	fence_action(conn, options, set_power_status, get_power_status, get_outlets_status)

	## Logout from system
	try:
		conn.sendline("quit")
		conn.close()
	except exceptions.OSError:
		pass
	except pexpect.ExceptionPexpect:
		pass

if __name__ == "__main__":
	main()
