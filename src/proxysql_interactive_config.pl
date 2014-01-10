#!/usr/bin/perl
use strict;
use warnings;

my %proxycfg = () ; 

my $bool="";
my $dbglvl=0;
my $configfile="";

my %defaults = (
	core_dump_file_size => 0,
	debug => 0,
	stack_size => 524288,
	proxy_mysql_port => 6033,
	proxy_admin_port => 6032,
	proxy_admin_user => "admin",
	proxy_admin_password => "admin",
	mysql_socket => "/tmp/proxysql.sock",
	mysql_query_cache_partitions => 16,
	mysql_query_cache_default_timeout => 1,
	mysql_query_cache_size => 67108864 ,
	mysql_threads => 1,
	mysql_poll_timeout => 10000,
	mysql_max_query_size => 1048576,
	mysql_max_resultset_size => 1048576,
	net_buffer_size => 8192,
	backlog => 2000,
	mysql_connection_pool_enabled => 1,
	mysql_wait_timeout => 28800 ,
	mysql_usage_user => "proxy",
	mysql_usage_password => "proxy",
	config_file => "proxysql.cnf"
);
my $cpus=`cat /proc/cpuinfo | egrep '^processor' | wc -l`;
$defaults{'mysql_threads'}=$cpus*2;


sub intro {
print "

Interactive script to configure ProxySQL,
High Performance and High Availability proxy for MySQL


";
}


sub config_to_string {
$configfile="
#
# ProxySQL config file
#
# Generated using proxysql_interactive_config.pl
#
[global]
core_dump_file_size=$proxycfg{'core_dump_file_size'}
debug=$proxycfg{'debug'}
stack_size=$proxycfg{'stack_size'}
proxy_admin_port=$proxycfg{'proxy_admin_port'}
proxy_admin_user=$proxycfg{'proxy_admin_user'}
proxy_admin_password=$proxycfg{'proxy_admin_password'}
net_buffer_size=$proxycfg{'net_buffer_size'}
backlog=$proxycfg{'backlog'}

[mysql]
mysql_threads=$proxycfg{'mysql_threads'}
proxy_mysql_port=$proxycfg{'proxy_mysql_port'}
mysql_socket=$proxycfg{'mysql_socket'}
mysql_query_cache_partitions=$proxycfg{'mysql_query_cache_partitions'}
mysql_query_cache_default_timeout=$proxycfg{'mysql_query_cache_default_timeout'}
mysql_query_cache_size=$proxycfg{'mysql_query_cache_size'}
mysql_poll_timeout=$proxycfg{'mysql_poll_timeout'}
mysql_max_query_size=$proxycfg{'mysql_max_query_size'}
mysql_max_resultset_size=$proxycfg{'mysql_max_resultset_size'}
mysql_connection_pool_enabled=$proxycfg{'mysql_connection_pool_enabled'}
mysql_wait_timeout=$proxycfg{'mysql_wait_timeout'}
mysql_servers=$proxycfg{'mysql_servers'}
mysql_usage_user=$proxycfg{'mysql_usage_user'}
mysql_usage_password=$proxycfg{'mysql_usage_password'}
$proxycfg{'mysql_users'}
[debug]
debug_generic=$dbglvl
debug_net=$dbglvl
debug_pkt_array=$dbglvl
debug_memory=$dbglvl
debug_mysql_com=$dbglvl
debug_mysql_connection=$dbglvl
debug_mysql_server=$dbglvl
debug_admin=$dbglvl
debug_mysql_auth=$dbglvl
"
};


sub conf_general {
print "

Generic options:
- core_dump_file_size : maximum size of core dump in case of crash
- stack_size : stack size allocated for each thread
- error_log : log file for error messages (not implemented yet)

";
do {
	print "\tcore_dump_file_size [$defaults{'core_dump_file_size'}]: ";
	my $input = <STDIN>;
	chomp $input;
	if ( $input =~ /^\d+$/ ) { $proxycfg{'core_dump_file_size'}=$input }
	if ( $input =~ /^$/ ) { $proxycfg{'core_dump_file_size'}=$defaults{'core_dump_file_size'} }
} until (defined $proxycfg{'core_dump_file_size'});
do {
	print "\tstack_size (65536-8388608) [$defaults{'stack_size'}]: ";
	my $input = <STDIN>;
	chomp $input;
	if ( ( $input =~ /^\d+$/ ) && ( $input >= 65536 ) && ( $input <= 8388608) ) { $proxycfg{'stack_size'}=$input }
	if ( $input =~ /^$/ ) { $proxycfg{'stack_size'}=$defaults{'stack_size'} }
} until (defined $proxycfg{'stack_size'});
}

sub conf_sockets {
print "

Clients can communicate with ProxySQL through 2 different sockets:
- proxy_mysql_port : TCP socket for MySQL traffic : default is 6033
- mysql_socket : Unix Domanin socket : default is /tmp/proxysql.sock

";
do {
	print "\tproxy_mysql_port [$defaults{'proxy_mysql_port'}]: ";
	my $input = <STDIN>;
	chomp $input;
	if ( $input =~ /^\d+$/ ) { $proxycfg{'proxy_mysql_port'}=$input }
	if ( $input =~ /^$/ ) { $proxycfg{'proxy_mysql_port'}=$defaults{'proxy_mysql_port'} }
} until (defined $proxycfg{'proxy_mysql_port'});
{
	print "\tmysql_socket [$defaults{'mysql_socket'}]: ";
	my $input = <STDIN>;
	chomp $input;
	if ( $input =~ /^$/ ) { $proxycfg{'mysql_socket'}=$defaults{'mysql_socket'} }
	else { $proxycfg{'mysql_socket'}=$input; }
};
}

sub conf_admin {
print "

ProxySQL uses an admin interface for runtime configuration and to export statistics.
Such interface uses the MySQL protocol and can be used by any MySQL client.
Options:
- proxy_admin_port : TCP socket for Administration : default is proxy_mysql_port-1 (6032)
- proxy_admin_user : username for authentication ( this is not a mysql user )
- proxy_admin_password : password for the user specified in proxy_admin_user

";
$defaults{'proxy_admin_port'}=$proxycfg{'proxy_mysql_port'}-1;
do {
	print "\tproxy_admin_port [$defaults{'proxy_admin_port'}]: ";
	my $input = <STDIN>;
	chomp $input;
	if ( $input =~ /^\d+$/ ) { $proxycfg{'proxy_admin_port'}=$input }
	if ( $input =~ /^$/ ) { $proxycfg{'proxy_admin_port'}=$defaults{'proxy_admin_port'} }
} until (defined $proxycfg{'proxy_admin_port'});
{
	print "\tproxy_admin_user [$defaults{'proxy_admin_user'}]: ";
	my $input = <STDIN>;
	chomp $input;
	if ( $input =~ /^$/ ) { $proxycfg{'proxy_admin_user'}=$defaults{'proxy_admin_user'} }
	else { $proxycfg{'proxy_admin_user'}=$input; }
};
{
	print "\tproxy_admin_password [$defaults{'proxy_admin_password'}]: ";
	my $input = <STDIN>;
	chomp $input;
	if ( $input =~ /^$/ ) { $proxycfg{'proxy_admin_password'}=$defaults{'proxy_admin_password'} }
	else { $proxycfg{'proxy_admin_password'}=$input; }
};
}

sub conf_query_cache {
print "

ProxySQL allows to cache SELECT statements executed by the application.
Query cache is configured through:
- mysql_query_cache_partitions : defines the number of partitions, reducing contention
- mysql_query_cache_default_timeout : defaults TTL for queries without explicit TTL
- mysql_query_cache_size : total amount of memory allocable for query cache

";
do {
	print "\tmysql_query_cache_partitions (1-64) [$defaults{'mysql_query_cache_partitions'}]: ";
	my $input = <STDIN>;
	chomp $input;
	if ( ( $input =~ /^\d+$/ ) && ( $input >= 1 ) && ( $input <= 64 ) ) { $proxycfg{'mysql_query_cache_partitions'}=$input }
	if ( $input =~ /^$/ ) { $proxycfg{'mysql_query_cache_partitions'}=$defaults{'mysql_query_cache_partitions'} }
} until (defined $proxycfg{'mysql_query_cache_partitions'});
do {
	print "\tmysql_query_cache_default_timeout (0-315360000) [$defaults{'mysql_query_cache_default_timeout'}]: ";
	my $input = <STDIN>;
	chomp $input;
	if ( ( $input =~ /^\d+$/ ) && ( $input >= 0 ) && ( $input <= 315360000 ) ) { $proxycfg{'mysql_query_cache_default_timeout'}=$input }
	if ( $input =~ /^$/ ) { $proxycfg{'mysql_query_cache_default_timeout'}=$defaults{'mysql_query_cache_default_timeout'} }
} until (defined $proxycfg{'mysql_query_cache_default_timeout'});
do {
	print "\tmysql_query_cache_size (1048576-10737418240) [$defaults{'mysql_query_cache_size'}]: ";
	my $input = <STDIN>;
	chomp $input;
	if ( ( $input =~ /^\d+$/ ) && ( $input >= 1048576 ) && ( $input <= 10737418240 ) ) { $proxycfg{'mysql_query_cache_size'}=$input }
	if ( $input =~ /^$/ ) { $proxycfg{'mysql_query_cache_size'}=$defaults{'mysql_query_cache_size'} }
} until (defined $proxycfg{'mysql_query_cache_size'});
}


sub conf_network {
print "

Several options define the network behaviour of ProxySQL:
- mysql_threads : defines how many threads will process MySQL traffic
- mysql_poll_timeout : poll() timeout (millisecond)
- mysql_max_query_size : maximum length of a query to be analyzed
- mysql_max_resultset_size : maximum size of resultset for caching and buffering
- net_buffer_size : internal buffer for network I/O
- backlog : listen() backlog

";

do {
	print "\tmysql_threads (1-128) [$defaults{'mysql_threads'}]: ";
	my $input = <STDIN>;
	chomp $input;
	if ( ( $input =~ /^\d+$/ ) && ( $input <= 128 ) ) { $proxycfg{'mysql_threads'}=$input }
	if ( $input =~ /^$/ ) { $proxycfg{'mysql_threads'}=$defaults{'mysql_threads'} }
} until (defined $proxycfg{'mysql_threads'});
do {
	print "\tmysql_poll_timeout (100-1000000) [$defaults{'mysql_poll_timeout'}]: ";
	my $input = <STDIN>;
	chomp $input;
	if ( ( $input =~ /^\d+$/ ) && ( $input >= 100 ) && ( $input <= 1000000 ) ) { $proxycfg{'mysql_poll_timeout'}=$input }
	if ( $input =~ /^$/ ) { $proxycfg{'mysql_poll_timeout'}=$defaults{'mysql_poll_timeout'} }
} until (defined $proxycfg{'mysql_poll_timeout'});
do {
	print "\tmysql_max_query_size (1-16777210) [$defaults{'mysql_max_query_size'}]: ";
	my $input = <STDIN>;
	chomp $input;
	if ( ( $input =~ /^\d+$/ ) && ( $input >= 1 ) && ( $input <= 16777210 ) ) { $proxycfg{'mysql_max_query_size'}=$input }
	if ( $input =~ /^$/ ) { $proxycfg{'mysql_max_query_size'}=$defaults{'mysql_max_query_size'} }
} until (defined $proxycfg{'mysql_max_query_size'});
do {
	print "\tmysql_max_resultset_size (1-1073741824) [$defaults{'mysql_max_resultset_size'}]: ";
	my $input = <STDIN>;
	chomp $input;
	if ( ( $input =~ /^\d+$/ ) && ( $input >= 1 ) && ( $input <= 1073741824 ) ) { $proxycfg{'mysql_max_resultset_size'}=$input }
	if ( $input =~ /^$/ ) { $proxycfg{'mysql_max_resultset_size'}=$defaults{'mysql_max_resultset_size'} }
} until (defined $proxycfg{'mysql_max_resultset_size'});
do {
	print "\tnet_buffer_size (1024-16777216) [$defaults{'net_buffer_size'}]: ";
	my $input = <STDIN>;
	chomp $input;
	if ( ( $input =~ /^\d+$/ ) && ( $input >= 1024 ) && ( $input <= 16777216 ) ) { $proxycfg{'net_buffer_size'}=$input }
	if ( $input =~ /^$/ ) { $proxycfg{'net_buffer_size'}=$defaults{'net_buffer_size'} }
} until (defined $proxycfg{'net_buffer_size'});
$proxycfg{'net_buffer_size'}=int($proxycfg{'net_buffer_size'}/1024)*1024;
do {
	print "\tbacklog (50-10000) [$defaults{'backlog'}]: ";
	my $input = <STDIN>;
	chomp $input;
	if ( ( $input =~ /^\d+$/ ) && ( $input >= 50 ) && ( $input <= 10000 ) ) { $proxycfg{'backlog'}=$input }
	if ( $input =~ /^$/ ) { $proxycfg{'backlog'}=$defaults{'backlog'} }
} until (defined $proxycfg{'backlog'});
}



sub conf_conn_poll {
print "

ProxySQL implements an internal connection pool. Configurable with:
- mysql_connection_pool_enabled : enables the connection pool if set to 1
- mysql_wait_timeout : timeout to drop unused connections

";
do {
	print "\tmysql_connection_pool_enabled (0-1) [$defaults{'mysql_connection_pool_enabled'}]: ";
	my $input = <STDIN>;
	chomp $input;
	if ( ( $input =~ /^\d+$/ ) && ( $input <= 1 ) ) { $proxycfg{'mysql_connection_pool_enabled'}=$input }
	if ( $input =~ /^$/ ) { $proxycfg{'mysql_connection_pool_enabled'}=$defaults{'mysql_connection_pool_enabled'} }
} until (defined $proxycfg{'mysql_connection_pool_enabled'});
do {
	print "\tmysql_wait_timeout (1-31536000) [$defaults{'mysql_wait_timeout'}]: ";
	my $input = <STDIN>;
	chomp $input;
	if ( ( $input =~ /^\d+$/ ) && ( $input >= 1 ) && ( $input <= 31536000 ) ) { $proxycfg{'mysql_wait_timeout'}=$input }
	if ( $input =~ /^$/ ) { $proxycfg{'mysql_wait_timeout'}=$defaults{'mysql_wait_timeout'} }
} until (defined $proxycfg{'mysql_wait_timeout'});
}


sub conf_mysql_backends1 {
print "

ProxySQL connects to various mysqld instances that form the backend.
- mysql_servers : list of mysqld servers in the format host:port;host:port;...

";
$proxycfg{'mysql_servers'}="";
my $cont=0;
my $srvcnt=1;
while ($cont==0) {
	my $input;
	print "\tHostname[:port] of backend#$srvcnt : ";
	$input = <STDIN>;
	chomp $input;
	if ( $input =~ /^$/ ) { next; }
	my $server=$input;
	my $port="";
	if ( $input !~ /:\d+$/ ) {
		do {
			print "\t\tport for server $server [3306]: ";
			$input = <STDIN>;
			chomp $input;
			if ( $input =~ /^\d+$/ ) { $port=$input }
			if ( $input =~ /^$/ ) { $port=3306 }
		} until ($port ne "");
	}
	$server=$server.":".$port;
	if ($srvcnt>1) { $server=";".$server; }
	$proxycfg{'mysql_servers'}.=$server;
	$srvcnt+=1;
	if ($proxycfg{'mysql_servers'} ne "") {
		my $more="";		
		do {
			print "\tWould you like to add another backend server (Y-N) [N]: ";
			my $input = <STDIN>;
			chomp $input;
			if ( $input =~ /^$/ ) { $more="N" }	
			if ( $input =~ /^N(o|)$/i ) { $more="N" }
			if ( $input =~ /^Y(es|)$/i ) { $more="Y" }	
		} until ($more ne "");
		if ($more eq "N") { $cont=1; }
	}
}
}


sub conf_mysql_users {
$proxycfg{'mysql_users'}="[mysql users]\n";
print "

ProxySQL authenticates clients' connections, and then uses the same credentials to connect to the backends.
ProxySQL needs to know clients' usernames and passwords because a single client connection can generate multiple connections to the backend.

";
my $cont=0;
my $usercnt=1;
while ($cont==0) {
	my $input;
	print "\tUsername for user#$usercnt : ";
	$input = <STDIN>;
	chomp $input;
	if ( $input =~ /^$/ ) { next; }
	my $user=$input;
	print "\tPassword for user $user : ";
	$input = <STDIN>;
	chomp $input;
	my $password=$input;
	$proxycfg{'mysql_users'}.="$user=$password\n";
	$usercnt+=1;
	my $more="";		
	do {
		print "\tWould you like to add another user (Y-N) [N]: ";
		my $input = <STDIN>;
		chomp $input;
		if ( $input =~ /^$/ ) { $more="N" }	
		if ( $input =~ /^N(o|)$/i ) { $more="N" }
		if ( $input =~ /^Y(es|)$/i ) { $more="Y" }	
	} until ($more ne "");
	if ($more eq "N") { $cont=1; }
}
}

sub conf_mysql_backends2 {
print "

Few options specify how to connect to the backend:
- mysql_usage_user : user used by ProxySQL to connect to the backend to verify its status
- mysql_usage_password : password for user specified in mysql_usage_user

Note:
the user specified in mysql_usage_user needs only USAGE privilege, and you can create the user with GRANT USAGE

";
{
	print "\tmysql_usage_user [$defaults{'mysql_usage_user'}]: ";
	my $input = <STDIN>;
	chomp $input;
	if ( $input =~ /^$/ ) { $proxycfg{'mysql_usage_user'}=$defaults{'mysql_usage_user'} }
	else { $proxycfg{'mysql_usage_user'}=$input; }
};
{
	print "\tmysql_usage_password [$defaults{'mysql_usage_password'}]: ";
	my $input = <STDIN>;
	chomp $input;
	if ( $input =~ /^$/ ) { $proxycfg{'mysql_usage_password'}=$defaults{'mysql_usage_password'} }
	else { $proxycfg{'mysql_usage_password'}=$input; }
};
print "
Note (again!):
The user specified in mysql_usage_user needs only USAGE privilege
You can create the user with GRANT USAGE ON *.* TO '$proxycfg{'mysql_usage_user'}'\@'<proxysqlip>' IDENTIFIED BY '$proxycfg{'mysql_usage_password'}';

";

}

sub enable_verb {
$bool="";
print "\nIf you compiled ProxySQL with debug information (enabled by default) you can enable debug verbosity.\n\n";
do {
	print "\tWould you like to enable debug verbosity? (Y-N) [N]: ";
	my $input = <STDIN>;
	chomp $input;
	if ( $input =~ /^$/ ) { $proxycfg{'debug'}=0 }	
	if ( $input =~ /^N(o|)$/i ) { $proxycfg{'debug'}=0 }
	if ( $input =~ /^Y(es|)$/i ) { $proxycfg{'debug'}=1 }
} until (defined $proxycfg{'debug'});
if ($proxycfg{'debug'}==1) {
print "

Several modules can be debugged and each of them can be configured with a different verbosity level.
You can now configure the default verbosity level, and you can fine tune it later on

";
do {
    print "\tdefault debug level (0-9) [$dbglvl]: ";
    my $input = <STDIN>;
    chomp $input;
    if ( ( $input =~ /^\d+$/ ) && ( $input >= 0 ) && ($input <= 9 ) ) { $proxycfg{'dbglvl'}=$input }
    if ( $input =~ /^$/ ) { $proxycfg{'dbglvl'}=$dbglvl; }
} until (defined $proxycfg{'dbglvl'});
$dbglvl=$proxycfg{'dbglvl'};
}
}


sub save_to_file {
$bool="";
do {
	print "\n\nWould you like to write a configuration file? (Y-N) [Y]: ";
	my $input = <STDIN>;
	chomp $input;
	if ( $input =~ /^$/ ) { $bool="Y" }	
	if ( $input =~ /^N(o|)$/i ) { $bool="N" }
	if ( $input =~ /^Y(es|)$/i ) { $bool="Y" }
} until ($bool ne "");
if ($bool eq "Y") {
my $filewritten=0;
do {
	print "\tconfig filename [$defaults{'config_file'}]: ";
	my $input = <STDIN>;
	chomp $input;
	if ( $input =~ /^$/ ) { $proxycfg{'config_file'}=$defaults{'config_file'} }
	else { $proxycfg{'config_file'}=$input; }
	if (-e $proxycfg{'config_file'}) {
		print "Error: file $proxycfg{'config_file'} Exists!\n";
 	} else {
		open (MYFILE, "> $proxycfg{'config_file'}");
		print MYFILE $configfile;
		close (MYFILE); 
		$filewritten=1;
	}
} until ($filewritten==1 );
}

}

sub main {
intro();
conf_general();
conf_sockets();
conf_admin();
conf_query_cache();
conf_network();
conf_conn_poll();
conf_mysql_backends1();
conf_mysql_backends2();
conf_mysql_users;
print "\nBasic configuration completed!\n\n";
enable_verb();
config_to_string();
print "\n\n$configfile";
save_to_file();
print "\nConfiguration completed!\nQuit\n\n";
}

if(!caller) { exit(main(@ARGV)); }
