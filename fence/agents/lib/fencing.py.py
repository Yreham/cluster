#!/usr/bin/python

import sys, getopt, time, os
import pexpect, re
import telnetlib
import atexit

## do not add code here.
#BEGIN_VERSION_GENERATION
RELEASE_VERSION = "New fence lib agent - test release on steroids"
REDHAT_COPYRIGHT = ""
BUILD_DATE = "March, 2008"
#END_VERSION_GENERATION

POWER_TIMEOUT = 20
SHELL_TIMEOUT = 3
LOGIN_TIMEOUT = 5

LOG_MODE_VERBOSE = 100
LOG_MODE_QUIET = 0

EC_BAD_ARGS        = 2
EC_LOGIN_DENIED    = 3
EC_CONNECTION_LOST = 4
EC_TIMED_OUT       = 5
EC_WAITING_ON      = 6
EC_WAITING_OFF     = 7
EC_STATUS          = 8
EC_STATUS_HMC      = 9

TELNET_PATH = "/usr/bin/telnet"
SSH_PATH    = "/usr/bin/ssh"
SSL_PATH    = "@SBINDIR@/fence_nss_wrapper"

all_opt = {
	"help"    : {
		"getopt" : "h",
		"longopt" : "help",
		"help" : "-h, --help                     Display this help and exit",
		"order" : 54 },
	"version" : { 
		"getopt" : "V",
		"longopt" : "version",
		"help" : "-V, --version                  Output version information and exit",
		"order" : 53 },
	"quiet"   : {
		"getopt" : "q",
		"help" : "",
		"order" : 50 },
	"verbose" : {
		"getopt" : "v",
		"longopt" : "verbose",
		"help" : "-v, --verbose                  Verbose mode",
		"required" : "0",
		"shortdesc" : "Verbose mode",
		"order" : 51 },
	"debug" : {
		"getopt" : "D:",
		"longopt" : "debug-file", 
		"help" : "-D, --debug-file=<debugfile>   Debugging to output file",
		"order" : 52 },
	"agent"   : {
		"getopt" : "",
		"help" : "",
		"order" : 1 },
	"action" : {
		"getopt" : "o:",
		"longopt" : "action",
		"help" : "-o, --action=<action>          Action: status, reboot (default), off or on",
		"required" : "1",
		"shortdesc" : "Fencing Action",
		"default" : "reboot",
		"order" : 1 },
	"ipaddr" : {
		"getopt" : "a:",
		"longopt" : "ip",
		"help" : "-a, --ip=<ip>                  IP address or hostname of fencing device",
		"required" : "1",
		"shortdesc" : "IP Address or Hostname",
		"order" : 1 },
	"login" : {
		"getopt" : "l:",
		"longopt" : "username",
		"help" : "-l, --username=<name>          Login name",
		"required" : "?",
		"shortdesc" : "Login Name",
		"order" : 1 },
	"no_login" : {
		"getopt" : "",
		"help" : "",
		"order" : 1 },
	"no_password" : {
		"getopt" : "",
		"help" : "",
		"order" : 1 },
	"passwd" : {
		"getopt" : "p:",
		"longopt" : "password",
		"help" : "-p, --password=<password>      Login password or passphrase",
		"required" : "0",
		"shortdesc" : "Login password or passphrase",
		"order" : 1 },
	"passwd_script" : {
		"getopt" : "S:",
		"longopt" : "password-script=",
		"help" : "-S, --password-script=<script> Script to run to retrieve password",
		"required" : "0",
		"shortdesc" : "Script to retrieve password",
		"order" : 1 },
	"identity_file" : {
		"getopt" : "k:",
		"longopt" : "identity-file",
		"help" : "-k, --identity-file=<filename> Identity file (private key) for ssh ",
		"required" : "0",
		"shortdesc" : "Identity file for ssh",
		"order" : 1 },
	"module_name" : {
		"getopt" : "m:",
		"longopt" : "module-name",
		"help" : "-m, --module-name=<module>     DRAC/MC module name",
		"order" : 1 },
	"drac_version" : {
		"getopt" : "d:",
		"longopt" : "drac-version",
		"help" : "-D, --drac-version=<version>   Force DRAC version to use",
		"order" : 1 },
	"hmc_version" : {
		"getopt" : "H:",
		"longopt" : "hmc-version",
		"help" : "-H, --hmc-version=<version>   Force HMC version to use: 3, 4 (default)",
		"default" : "4", 
		"order" : 1 },
	"ribcl" : {
		"getopt" : "r:",
		"longopt" : "ribcl-version",
		"help" : "-r, --ribcl-version=<version>  Force ribcl version to use",
		"order" : 1 },
	"cmd_prompt" : {
		"getopt" : "c:",
		"longopt" : "command-prompt",
		"help" : "-c, --command-prompt=<prompt>  Force command prompt",
		"order" : 1 },
	"secure" : {
		"getopt" : "x",
		"longopt" : "ssh",
		"help" : "-x, --ssh                      Use ssh connection",
		"required" : "0",
		"shortdesc" : "SSH connection",
		"order" : 1 },
	"ssl" : {
		"getopt" : "z",
		"longopt" : "ssl",
		"help" : "-z, --ssl                      Use ssl connection",
		"required" : "0",
		"shortdesc" : "SSL connection",
		"order" : 1 },
	"port" : {
		"getopt" : "n:",
		"longopt" : "plug",
		"help" : "-n, --plug=<id>                Physical plug number on device or\n" + 
        "                                        name of virtual machine",
		"required" : "1",
		"shortdesc" : "Physical plug number or name of virtual machine",
		"order" : 1 },
	"switch" : {
		"getopt" : "s:",
		"longopt" : "switch",
		"help" : "-s, --switch=<id>              Physical switch number on device",
		"required" : "0",
		"shortdesc" : "Physical switch number on device",
		"order" : 1 },
	"partition" : {
		"getopt" : "n:",
		"help" : "-n <id>                        Name of the partition",
		"required" : "0",
		"shortdesc" : "Partition name",
		"order" : 1 },
	"managed" : {
		"getopt" : "s:",
		"help" : "-s <id>                        Name of the managed system",
		"required" : "0",
		"shortdesc" : "Managed system name",
		"order" : 1 },
	"test" : {
		"getopt" : "T",
		"help" : "",
		"order" : 1,
		"obsolete" : "use -o status instead" },
	"exec" : {
		"getopt" : "e:",
		"longopt" : "exec",
		"help" : "-e, --exec=<command>           Command to execute",
		"required" : "0",
		"shortdesc" : "Command to execute",
		"order" : 1 },
	"vmware_type" : {
		"getopt" : "d:",
		"longopt" : "vmware_type",
		"help" : "-d, --vmware_type=<type>       Type of VMware to connect",
		"required" : "0",
		"shortdesc" : "Type of VMware to connect",
		"order" : 1 },
	"vmware_datacenter" : {
		"getopt" : "s:",
		"longopt" : "vmware-datacenter",
		"help" : "-s, --vmware-datacenter=<dc>   VMWare datacenter filter",
		"required" : "0",
		"shortdesc" : "Show only machines in specified datacenter",
		"order" : 2 },
	"snmp_version" : {
		"getopt" : "d:",
		"longopt" : "snmp-version",
		"help" : "-d, --snmp-version=<ver>       Specifies SNMP version to use",
		"required" : "0",
		"shortdesc" : "Specifies SNMP version to use (1,2c,3)",
		"order" : 1 },
	"community" : {
		"getopt" : "c:",
		"longopt" : "community",
		"help" : "-c, --community=<community>    Set the community string",
		"required" : "0",
		"shortdesc" : "Set the community string",
		"order" : 1},
	"snmp_auth_prot" : {
		"getopt" : "b:",
		"longopt" : "snmp-auth-prot",
		"help" : "-b, --snmp-auth-prot=<prot>    Set authentication protocol (MD5|SHA)",
		"required" : "0",
		"shortdesc" : "Set authentication protocol (MD5|SHA)",
		"order" : 1},
	"snmp_sec_level" : {
		"getopt" : "E:",
		"longopt" : "snmp-sec-level",
		"help" : "-E, --snmp-sec-level=<level>   Set security level\n"+
		"                                  (noAuthNoPriv|authNoPriv|authPriv)",
		"required" : "0",
		"shortdesc" : "Set security level (noAuthNoPriv|authNoPriv|authPriv)",
		"order" : 1},
	"snmp_priv_prot" : {
		"getopt" : "B:",
		"longopt" : "snmp-priv-prot",
		"help" : "-B, --snmp-priv-prot=<prot>    Set privacy protocol (DES|AES)",
		"required" : "0",
		"shortdesc" : "Set privacy protocol (DES|AES)",
		"order" : 1},
	"snmp_priv_passwd" : {
		"getopt" : "P:",
		"longopt" : "snmp-priv-passwd",
		"help" : "-P, --snmp-priv-passwd=<pass>  Set privacy protocol password",
		"required" : "0",
		"shortdesc" : "Set privacy protocol password",
		"order" : 1},
	"snmp_priv_passwd_script" : {
		"getopt" : "R:",
		"longopt" : "snmp-priv-passwd-script",
		"help" : "-R, --snmp-priv-passwd-script  Script to run to retrieve privacy password",
		"required" : "0",
		"shortdesc" : "Script to run to retrieve privacy password",
		"order" : 1},
	"force_ipv4" : {
		"getopt" : "4",
		"longopt" : "force-ipv4",
		"help" : "-4, --force-ipv4               Forces agent to use IPv4 addresses only",
		"required" : "0",
		"shortdesc" : "Forces agent to use IPv4 addresses only",
		"order" : 1 },
	"force_ipv6" : {
		"getopt" : "6",
		"longopt" : "force-ipv6",
		"help" : "-6, --force-ipv6               Forces agent to use IPv6 addresses only",
		"required" : "0",
		"shortdesc" : "Forces agent to use IPv6 addresses only",
		"order" : 1 },
	"udpport" : {
		"getopt" : "u:",
		"longopt" : "udpport",
		"help" : "-u, --udpport                  UDP/TCP port to use",
		"required" : "0",
		"shortdesc" : "UDP/TCP port to use for connection with device",
		"order" : 1},
	"separator" : {
		"getopt" : "C:",
		"longopt" : "separator",
		"help" : "-C, --separator=<char>         Separator for CSV created by 'list' operation",
		"default" : ",", 
		"order" : 100 }
}

class fspawn(pexpect.spawn):
	def log_expect(self, options, pattern, timeout):
		result = self.expect(pattern, timeout)
		if options["log"] >= LOG_MODE_VERBOSE:
			options["debug_fh"].write(self.before + self.after)
		return result

def atexit_handler():
	try:
		sys.stdout.close()
		os.close(1)
	except IOError:
		sys.stderr.write("%s failed to close standard output\n"%(sys.argv[0]))
		sys.exit(1)

def version(command, release, build_date, copyright_notice):
	print command, " ", release, " ", build_date
	if len(copyright_notice) > 0:
		print copyright_notice

def fail_usage(message = ""):
	if len(message) > 0:
		sys.stderr.write(message+"\n")
	sys.stderr.write("Please use '-h' for usage\n")
	sys.exit(EC_BAD_ARGS)

def fail(error_code):
	message = {
		EC_LOGIN_DENIED : "Unable to connect/login to fencing device",
		EC_CONNECTION_LOST : "Connection lost",
		EC_TIMED_OUT : "Connection timed out",
		EC_WAITING_ON : "Failed: Timed out waiting to power ON",
		EC_WAITING_OFF : "Failed: Timed out waiting to power OFF",
		EC_STATUS : "Failed: Unable to obtain correct plug status or plug is not available",
		EC_STATUS_HMC : "Failed: Either unable to obtaion correct plug status, partition is not available or incorrect HMC version used"
	}[error_code] + "\n"
	sys.stderr.write(message)
	sys.exit(error_code)

def usage(avail_opt):
	global all_opt

	print "Usage:"
	print "\t" + os.path.basename(sys.argv[0]) + " [options]"
	print "Options:"

	sorted_list = [ (key, all_opt[key]) for key in avail_opt ]
	sorted_list.sort(lambda x, y: cmp(x[1]["order"], y[1]["order"]))

	for key, value in sorted_list:
		if len(value["help"]) != 0:
			print "   " + value["help"]

def metadata(avail_opt):
	global all_opt

	sorted_list = [ (key, all_opt[key]) for key in avail_opt ]
	sorted_list.sort(lambda x, y: cmp(x[1]["order"], y[1]["order"]))

	print "<?xml version=\"1.0\" ?>"
	print "<resource-agent name=\"" + os.path.basename(sys.argv[0]) + "\" >"
	print "<parameters>"
	for option, value in sorted_list:
		if all_opt[option].has_key("shortdesc"):
			print "\t<parameter name=\"" + option + "\" unique=\"1\" required=\"" + all_opt[option]["required"] + "\">"

			default = ""
			if all_opt[option].has_key("default"):
				default = "default=\""+all_opt[option]["default"]+"\""

			if all_opt[option]["getopt"].count(":") > 0:
				print "\t\t<content type=\"string\" "+default+" />"
			else:
				print "\t\t<content type=\"boolean\" "+default+" />"
			print "\t\t<shortdesc lang=\"en\">" + all_opt[option]["shortdesc"] + "</shortdesc>"
			print "\t</parameter>"
	print "</parameters>"
	print "</resource-agent>"

def process_input(avail_opt):
	global all_opt

	##
	## Set standard environment
	#####
	os.putenv("LANG", "C")
	os.putenv("LC_ALL", "C")

	##
	## Prepare list of options for getopt
	#####
	getopt_string = ""
	longopt_list = [ ]
	for k in avail_opt:
		if all_opt.has_key(k):
			getopt_string += all_opt[k]["getopt"]
		else:
			fail_usage("Parse error: unknown option '"+k+"'")

		if all_opt.has_key(k) and all_opt[k].has_key("longopt"):
			if all_opt[k]["getopt"].endswith(":"):
				longopt_list.append(all_opt[k]["longopt"] + "=")
			else:
				longopt_list.append(all_opt[k]["longopt"])

	##
	## Read options from command line or standard input
	#####
	if len(sys.argv) > 1:
		try:
			opt, args = getopt.gnu_getopt(sys.argv[1:], getopt_string, longopt_list)
		except getopt.GetoptError, error:
			fail_usage("Parse error: " + error.msg)

		## Transform longopt to short one which are used in fencing agents
		#####
		old_opt = opt
		opt = { }
		for o in dict(old_opt).keys():
			if o.startswith("--"):
				for x in all_opt.keys():
					if all_opt[x].has_key("longopt") and "--" + all_opt[x]["longopt"] == o:
						opt["-" + all_opt[x]["getopt"].rstrip(":")] = dict(old_opt)[o]
			else:
				opt[o] = dict(old_opt)[o]

		## Compatibility Layer
		#####
		z = dict(opt)
		if z.has_key("-T") == 1:
			z["-o"] = "status"

		opt = z
		##
		#####
	else:
		opt = { }
		name = ""
		for line in sys.stdin.readlines():
			line = line.strip()
			if ((line.startswith("#")) or (len(line) == 0)):
				continue

			(name, value) = (line + "=").split("=", 1)
			value = value[:-1]

			## Compatibility Layer
			######
			if name == "blade":
				name = "port"
			elif name == "option":
				name = "action"
			elif name == "fm":
				name = "port"
			elif name == "hostname":
				name = "ipaddr"

			##
			######
			if avail_opt.count(name) == 0:
				fail_usage("Parse error: Unknown option '"+line+"'")

			if all_opt[name]["getopt"].endswith(":"):
				opt["-"+all_opt[name]["getopt"].rstrip(":")] = value
			elif ((value == "1") or (value.lower() == "yes")):
				opt["-"+all_opt[name]["getopt"]] = "1"
	return opt

##
## This function checks input and answers if we want to have same answers 
## in each of the fencing agents. It looks for possible errors and run
## password script to set a correct password
######
def check_input(device_opt, opt):
	global all_opt

	options = dict(opt)
	options["device_opt"] = device_opt

	## Set requirements that should be included in metadata
	#####
	if device_opt.count("login") and device_opt.count("no_login") == 0:
		all_opt["login"]["required"] = "1"
	else:
		all_opt["login"]["required"] = "0"

	## Process special options (and exit)
	#####
	if options.has_key("-h"): 
		usage(device_opt)
		sys.exit(0)

	if options.has_key("-o") and options["-o"].lower() == "metadata":
		metadata(device_opt)
		sys.exit(0)

	if options.has_key("-V"):
		print RELEASE_VERSION, BUILD_DATE
		print REDHAT_COPYRIGHT
		sys.exit(0)

	## Set default values
	#####
	for opt in device_opt:
		if all_opt[opt].has_key("default"):
			getopt = "-" + all_opt[opt]["getopt"].rstrip(":")
			if 0 == options.has_key(getopt):
				options[getopt] = all_opt[opt]["default"]

	options["-o"]=options["-o"].lower()

	if options.has_key("-v"):
		options["log"] = LOG_MODE_VERBOSE
	else:
		options["log"] = LOG_MODE_QUIET

	if 0 == ["on", "off", "reboot", "status", "list", "monitor"].count(options["-o"].lower()):
		fail_usage("Failed: Unrecognised action '" + options["-o"] + "'")

	if (0 == options.has_key("-l")) and device_opt.count("login") and (device_opt.count("no_login") == 0):
		fail_usage("Failed: You have to set login name")

	if 0 == options.has_key("-a"):
		fail_usage("Failed: You have to enter fence address")

	if (device_opt.count("no_password") == 0):
		if 0 == device_opt.count("identity_file"):
			if 0 == (options.has_key("-p") or options.has_key("-S")):
				fail_usage("Failed: You have to enter password or password script")
			else: 
				if 0 == (options.has_key("-p") or options.has_key("-S") or options.has_key("-k")):
					fail_usage("Failed: You have to enter password, password script or identity file")

	if 0 == options.has_key("-x") and 1 == options.has_key("-k"):
		fail_usage("Failed: You have to use identity file together with ssh connection (-x)")

	if 1 == options.has_key("-k"):
		if 0 == os.path.isfile(options["-k"]):
			fail_usage("Failed: Identity file " + options["-k"] + " does not exist")

	if (0 == ["list", "monitor"].count(options["-o"].lower())) and (0 == options.has_key("-n")) and (device_opt.count("port")):
		fail_usage("Failed: You have to enter plug number")

	if options.has_key("-S"):
		options["-p"] = os.popen(options["-S"]).read().rstrip()

	if options.has_key("-D"):
		try:
			options["debug_fh"] = file (options["-D"], "w")
		except IOError:
			fail_usage("Failed: Unable to create file "+options["-D"])

	if options.has_key("-v") and options.has_key("debug_fh") == 0:
		options["debug_fh"] = sys.stderr

	if options.has_key("-R"):
		options["-P"] = os.popen(options["-R"]).read().rstrip()

	return options
	
def wait_power_status(tn, options, get_power_fn):
	for dummy in xrange(POWER_TIMEOUT):
		if get_power_fn(tn, options) != options["-o"]:
			time.sleep(1)
		else:
			return 1
	return 0

def fence_action(tn, options, set_power_fn, get_power_fn, get_outlet_list = None):
	if (options["-o"] == "list") and (0 == options["device_opt"].count("port")) and (0 == options["device_opt"].count("partition")):
		print "N/A"
		return
	elif (options["-o"] == "list" and get_outlet_list == None):
		## @todo: exception?
		## This is just temporal solution, we will remove default value
		## None as soon as all existing agent will support this operation 
		print "NOTICE: List option is not working on this device yet"
		return
	elif (options["-o"] == "list") or ((options["-o"] == "monitor") and 1 == options["device_opt"].count("port")):
		outlets = get_outlet_list(tn, options)
		## keys can be numbers (port numbers) or strings (names of VM)
		for o in outlets.keys():
			(alias, status) = outlets[o]
			if options["-o"] != "monitor":
				print o + options["-C"] + alias	
		return

	status = get_power_fn(tn, options)

	if status != "on" and status != "off":  
		fail(EC_STATUS)

	if options["-o"] == "on":
		if status == "on":
			print "Success: Already ON"
		else:
			set_power_fn(tn, options)
			if wait_power_status(tn, options, get_power_fn):
				print "Success: Powered ON"
			else:
				fail(EC_WAITING_ON)
	elif options["-o"] == "off":
		if status == "off":
			print "Success: Already OFF"
		else:
			set_power_fn(tn, options)
			if wait_power_status(tn, options, get_power_fn):
				print "Success: Powered OFF"
			else:
				fail(EC_WAITING_OFF)
	elif options["-o"] == "reboot":
		if status != "off":
			options["-o"] = "off"
			set_power_fn(tn, options)
			if wait_power_status(tn, options, get_power_fn) == 0:
				fail(EC_WAITING_OFF)
		options["-o"] = "on"
		set_power_fn(tn, options)
		if wait_power_status(tn, options, get_power_fn) == 0:
			sys.stderr.write('Timed out waiting to power ON\n')
		print "Success: Rebooted"
	elif options["-o"] == "status":
		print "Status: " + status.upper()
	elif options["-o"] == "monitor":
		1

def fence_login(options):
	force_ipvx=""

	if (options.has_key("-6")):
		force_ipvx="-6 "

	if (options.has_key("-4")):
		force_ipvx="-4 "

	try:
		re_login = re.compile("(login: )|(Login Name:  )|(username: )|(User Name :)", re.IGNORECASE)
		re_pass  = re.compile("password", re.IGNORECASE)

		if options.has_key("-z"):
			command = '%s %s %s %s' % (SSL_PATH, force_ipvx, options["-a"], "443")
			conn = fspawn(command)
		elif options.has_key("-x") and 0 == options.has_key("-k"):
			command = '%s %s %s@%s' % (SSH_PATH, force_ipvx, options["-l"], options["-a"])
			if options.has_key("ssh_options"):
				command += ' ' + options["ssh_options"]
			conn = fspawn(command)

			if options.has_key("telnet_over_ssh"):
				#This is for stupid ssh servers (like ALOM) which behave more like telnet (ignore name and display login prompt)
				result = conn.log_expect(options, [ re_login, "Are you sure you want to continue connecting (yes/no)?" ], LOGIN_TIMEOUT)
				if result == 1:
					conn.sendline("yes") # Host identity confirm
					conn.log_expect(options, re_login, LOGIN_TIMEOUT)

				conn.sendline(options["-l"])
				conn.log_expect(options, re_pass, LOGIN_TIMEOUT)
			else:
				result = conn.log_expect(options, [ "ssword:", "Are you sure you want to continue connecting (yes/no)?" ], LOGIN_TIMEOUT)
				if result == 1:
					conn.sendline("yes")
					conn.log_expect(options, "ssword:", LOGIN_TIMEOUT)

			conn.sendline(options["-p"])
			conn.log_expect(options, options["-c"], LOGIN_TIMEOUT)
		elif options.has_key("-x") and 1 == options.has_key("-k"):
			command = '%s %s %s@%s -i %s' % (SSH_PATH, force_ipvx, options["-l"], options["-a"], options["-k"])
			if options.has_key("ssh_options"):
				command += ' ' + options["ssh_options"]
			conn = fspawn(command)

			result = conn.log_expect(options, [ options["-c"], "Are you sure you want to continue connecting (yes/no)?", "Enter passphrase for key '"+options["-k"]+"':" ], LOGIN_TIMEOUT)
			if result == 1:
				conn.sendline("yes")
				conn.log_expect(options, [ options["-c"], "Enter passphrase for key '"+options["-k"]+"':"] , LOGIN_TIMEOUT)
			if result != 0:
				if options.has_key("-p"):
					conn.sendline(options["-p"])
					conn.log_expect(options, options["-c"], LOGIN_TIMEOUT)
				else:
					fail_usage("Failed: You have to enter passphrase (-p) for identity file")
		else:
			conn = fspawn(TELNET_PATH)
			conn.send("set binary\n")
			conn.send("open %s\n"%(options["-a"]))
			conn.log_expect(options, re_login, LOGIN_TIMEOUT)
			conn.send(options["-l"]+"\r\n")
			conn.log_expect(options, re_pass, SHELL_TIMEOUT)
			conn.send(options["-p"]+"\r\n")
			conn.log_expect(options, options["-c"], SHELL_TIMEOUT)
	except pexpect.EOF:
		fail(EC_LOGIN_DENIED) 
	except pexpect.TIMEOUT:
		fail(EC_LOGIN_DENIED)
	return conn
