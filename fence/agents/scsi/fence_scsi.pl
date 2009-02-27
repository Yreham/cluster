#!/usr/bin/perl

use Getopt::Std;
use IPC::Open3;
use POSIX;

my $ME = $0;

END {
  defined fileno STDOUT or return;
  close STDOUT and return;
  warn "$ME: failed to close standard output: $!\n";
  $? ||= 1;
}

my @device_list;

$_ = $0;
s/.*\///;
my $pname = $_;

#BEGIN_VERSION_GENERATION
$RELEASE_VERSION="";
$REDHAT_COPYRIGHT="";
$BUILD_DATE="";
#END_VERSION_GENERATION

sub usage
{
    print "Usage\n";
    print "\n";
    print "$pname [options]\n";
    print "\n";
    print "Options\n";
    print "  -n <node>        IP address or hostname of node to fence\n";
    print "  -h               usage\n";
    print "  -V               version\n";
    print "  -v               verbose\n";

    exit 0;
}

sub version
{
    print "$pname $RELEASE_VERSION $BUILD_DATE\n";
    print "$REDHAT_COPYRIGHT\n" if ( $REDHAT_COPYRIGHT );

    exit 0;
}

sub fail
{
    ($msg)=@_;

    print $msg."\n" unless defined $opt_q;

    exit 1;
}

sub fail_usage
{
    ($msg)=@_;

    print STDERR $msg."\n" if $msg;
    print STDERR "Please use '-h' for usage.\n";

    exit 1;
}

sub get_cluster_id
{
    my $cluster_id;

    my ($in, $out, $err);
    my $cmd = "cman_tool status";

    my $pid = open3($in, $out, $err, $cmd) or die "$!\n";

    waitpid($pid, 0);

    die "Unable to execute cman_tool.\n" if ($?>>8);

    while (<$out>)
    {
	chomp;

	my ($name, $value) = split(/\s*:\s*/, $_);

	if ($name eq "Cluster Id")
	{
	    $cluster_id = $value;
	    last;
	}
    }

    close($in);
    close($out);
    close($err);

    print "[$pname]: get_cluster_id: cluster_id=$cluster_id\n" if $opt_v;

    return $cluster_id;
}

sub get_node_id
{
    ($node)=@_;

    my $node_id;

    my ($in, $out, $err);
    my $cmd = "ccs_tool query /cluster/clusternodes/clusternode[\@name=\\\"$node\\\"]/\@nodeid";

    my $pid = open3($in, $out, $err, $cmd) or die "$!\n";

    waitpid($pid, 0);

    die "Unable to execute ccs_tool.\n" if ($?>>8);

    while (<$out>) {
        chomp;
        $node_id = $_;
    }

    close($in);
    close($out);
    close($err);

    print "[$pname]: get_node_id ($node): node_id=$node_id\n" if $opt_v;

    return $node_id;
}

sub get_node_name
{
    print "[$pname]: get_hode_name: node_name=$opt_n\n" if $opt_v;

    return $opt_n;
}

sub get_host_id
{
    my $host_id;

    my ($in, $out, $err);
    my $cmd = "cman_tool status";

    my $pid = open3($in, $out, $err, $cmd) or die "$!\n";

    waitpid($pid, 0);

    die "Unable to execute cman_tool.\n" if ($?>>8);

    while (<$out>)
    {
	chomp;

	my ($name, $value) = split(/\s*:\s*/, $_);

	if ($name eq "Node ID")
	{
	    $host_id = $value;
	    last;
	}
    }

    close($in);
    close($out);
    close($err);

    print "[$pname]: get_host_id: host_id=$host_id\n" if $opt_v;

    return $host_id;
}

sub get_host_name
{
    my $host_name;

    my ($in, $out, $err);
    my $cmd = "cman_tool status";

    my $pid = open3($in, $out, $err, $cmd) or die "$!\n";

    waitpid($pid, 0);

    die "Unable to execute cman_tool.\n" if ($?>>8);

    while (<$out>)
    {
	chomp;

	my ($name, $value) = split(/\s*:\s*/, $_);

	if ($name eq "Node name")
	{
	    $host_name = $value;
	    last;
	}
    }

    close($in);
    close($out);
    close($err);

    print "[$pname]: get_host_name: host_name=$host_name\n" if $opt_v;

    return $host_name;
}

sub get_key
{
    ($node)=@_;

    my $cluster_id = get_cluster_id;
    my $node_id = get_node_id($node);

    if ($node_id == 0) {
	die "Unable to determine nodeid for $node.\n";
    }

    my $key = sprintf "%x%.4x", $cluster_id, $node_id;

    print "[$pname]: get_key ($node): key=$key\n" if $opt_v;

    return $key;
}

sub get_options_stdin
{
    my $opt;
    my $line = 0;

    while (defined($in = <>))
    {
	$_ = $in;
	chomp;

	# strip leading and trailing whitespace
	s/^\s*//;
	s/\s*$//;

	# skip comments
	next if /^#/;

	$line += 1;
	$opt = $_;

	next unless $opt;

	($name, $val) = split(/\s*=\s*/, $opt);

	if ($name eq "")
	{
	    print STDERR "parse error: illegal name in option $line\n";
	    exit 2;
	}
	elsif ($name eq "agent")
	{
	}
	elsif ($name eq "node")
	{
	    $opt_n = $val;
	}
	elsif ($name eq "nodename")
	{
	    $opt_n = $val;
	}
	elsif ($name eq "verbose")
	{
	    $opt_v = $val;
	}
	else
	{
	    fail "parse error: unknown option \"$opt\"";
	}
    }
}

sub get_key_list
{
    ($dev) = @_;

    my ($in, $out, $err);

    my $cmd = "sg_persist -d $dev -i -k";
    my $pid = open3($in, $out, $err, $cmd) or die "$!\n";

    waitpid($pid, 0);

    die "Unable to execute sg_persist.\n" if ($?>>8);

    my %key_list;

    while (<$out>)
    {
	chomp;
	if ($_ =~ /^\s*0x/)
	{
	    s/^\s+0x//;
	    s/\s+$//;

	    $key_list{$_} = 1;
	}
    }

    # DEBUG: use -v option
    #
    if ($opt_v)
    {
	my $count = keys %key_list;
	my $index = 0;

	print "[$pname]: get_key_list: found $count keys registered with $dev\n";

	for $key (keys %key_list)
	{
	    print "[$pname]:   ($index) key=$key\n";
	    $index++;
	}
    }

    close($in);
    close($out);
    close($err);

    return %key_list;
}

sub get_scsi_devices
{
    my ($in, $out, $err);

    my $cmd = "vgs --config 'global { locking_type = 0 }'" .
              "    --noheadings --separator : -o vg_attr,pv_name 2> /dev/null";

    my $pid = open3($in, $out, $err, $cmd) or die "$!\n";

    waitpid($pid, 0);

    die "Unable to execute vgs.\n" if ($?>>8);

    while (<$out>)
    {
	chomp;

	my ($vg_attrs, $dev) = split(/:/, $_);

	if ($vg_attrs =~ /.*c$/)
	{
	    $dev =~ s/\(.*\)//;
	    push(@device_list, $dev);
	}
    }

    # DEBUG: use -v flag
    #
    if ($opt_v)
    {
	my $count = scalar @device_list;
	my $index = 0;

	print "[$pname]: get_scsi_devices: found $count devices\n";

	for $dev (@device_list)
	{
	    print "[$pname]:   ($index) dev=$dev\n";
	    $index++;
	}
    }

    close($in);
    close($out);
    close($err);
}

sub check_sg_persist
{
    my ($in, $out, $err);
    my $cmd = "sg_persist -V";
    my $pid = open3($in, $out, $err, $cmd) or die "$!\n";

    waitpid($pid, 0);

    die "Unable to execute sg_persist.\n" if ($?>>8);

    close($in);
    close($out);
    close($err);
}

sub do_register
{
    ($dev, $key) = @_;

    my ($in, $out, $err);
    my $cmd = "sg_persist -n -d $dev -o -G -S $key";
    my $pid = open3($in, $out, $err, $cmd) or die "$!\n";

    waitpid($pid, 0);

    die "Unable to execute sg_persist ($dev).\n" if ($?>>8);

    close($in);
    close($out);
    close($err);
}

sub fence_node
{
    my $host_name = get_host_name();
    my $node_name = get_node_name();

    my $host_key = get_key($host_name);
    my $node_key = get_key($node_name);

    my ($in, $out, $err);

    foreach $dev (@device_list)
    {
	my %key_list = get_key_list($dev);

	# DEBUG: use -v option
	#
	if ($opt_v)
	{
	    print "[$pname]: unregister key 0x$node_key from device $dev\n";
	}

	if (!$key_list{$host_key})
	{
	    do_register($dev, $host_key);
	}

	if (!$key_list{$node_key})
	{
	    if ($opt_v)
	    {
		print "[$pname]: key 0x$node_key is not registered with device $dev\n";
	    }
	    next;
	}

	if ($host_key eq $node_key)
	{
	    $cmd = "sg_persist -n -d $dev -o -G -K $host_key -S 0";
	}
	else
	{
	    $cmd = "sg_persist -n -d $dev -o -A -K $host_key -S $node_key -T 5";
	}

	my $pid = open3($in, $out, $err, $cmd) or die "$!\n";

	waitpid($pid, 0);

	die "Unable to execute sg_persist ($dev).\n" if ($?>>8);

	close($in);
	close($out);
	close($err);
    }
}

### MAIN #######################################################

if (@ARGV > 0) {

    getopts("n:hqvV") || fail_usage;

    usage if defined $opt_h;
    version if defined $opt_V;

    fail_usage "Unknown parameter." if (@ARGV > 0);

    fail_usage "No '-n' flag specified." unless defined $opt_n;

} else {

    get_options_stdin();

    fail "failed: missing 'node'" unless defined $opt_n;

}

check_sg_persist;

get_scsi_devices;

fence_node;
