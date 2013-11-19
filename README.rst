============
Introduction
============

ProxySQL is a high performance proxy, currently for MySQL and forks (like Percona Server and MariaDB) only.
Future versions of ProxySQL will support a variety database backends.

Its development is driven by the lack of open source proxies that provide high performance.
Benchmarks can be found at http://www.proxysql.com


Installation
============


Dependencies
~~~~~~~~~~~~
Other than standard libraries, required libraries and header files are:

* libglib2 and libglib2-dev
* libmysqlclient and libmysqlclient-dev
* libpcre3 and libpcre3-dev

Compiling
~~~~~~~~~

Once download::

  cd src
  make

Note that no configure is available yet. You must check for missing dependencies.


Install
~~~~~~~

**make install** is not available yet.



Usage
~~~~~

Usage is the follow::

  $ ./proxysql --help
  Usage:
    proxysql [OPTION...] - High Performance Advanced Proxy for MySQL
  
  Help Options:
    -h, --help        Show help options
  
  Application Options:
    --admin-port      Administration port
    --mysql-port      MySQL proxy port
    -v, --verbose     Verbose level
    -c, --config      Configuration file


proxysql listens on 2 different ports:

* **--mysql-port** specifies the port that mysql clients should connect to
* **--admin-port** specifies the administration port : administration module is yet not implemented

Other options:

* **--verbose** specifies the verbosity level : feature not completely implemented
* **--config** specifies the configuration file

The configuration file is mandatory. It defaults to *proxysql.cnf* in the current directory, and if present there is no need to specify it on the command line.
Currently there is no strong input validation of the configuration file, and wrong parsing of it can cause proxysql to crash at startup.
proxysql does not daemonize yet, and it runs in foreground.


Configuration
-------------

Configuration file is key-value file , .ini-like config file ( see https://developer.gnome.org/glib/stable/glib-Key-value-file-parser.html for referene ).

Currently 5 groups are available, but only 4 parsed:

* **[global]** : generic configuration
* **[mysql]** : configuration options related to handling of mysql connections
* **[fundadb]** : configuration options for the internal storage used for caching . Do not edit
* **[debug]** : configuration options related to debugging . Not enabled yet . Do not edit
* **[mysql users]** : specify a list of users and their passwords used to connect to mysql servers


[global] configuration
~~~~~~~~~~~~~~~~~~~~~~

* **stack_size**

  Specify the stack size used by every thread created in proxysql , in bytes . Default is 262144 ( 256KB ) , minimum is 65536 ( 64KB ) , and no maximum is defined.

  The default stack_size in Linux is 8MB. Starting hundreds of connections/threads will quickly eat all memory so we need to lower this down to be more memory efficient.

* **net_buffer_size**

  Each connection to proxysql creates a so called MySQL data stream. Each MySQL data stream has 2 buffers for recv and send. *net_buffer_size* defines the size of each of these buffers. Each connection from proxysql to a mysql server needs a MySQL data stream. Each client connection can have a different number of MySQL data stream associated to it:

  - 1 : The client connects to proxysql and this one is able to serve each request from its own cache. No connections are established to mysql server.

  - 2 : The client connects to proxysql and this one needs to connect to a mysql server to serve requests from client.

  - 3 : The client connects to proxysql and this one needs to connect to two mysql servers, a master and a slave.

  That means that each client connection needs 1, 2 or 3 MySQL data streams, for a total of 2, 4 or 6 network buffers. Increasing this variables boost performance in case of large dataset, at the cost of additional memory usage. Default is 8192 (8KB), minimum is 1024 (1KB), and no maximum is defined.

* **proxy_admin_port**

  **Unused**. It will define the administrative port.

* **backlog**

  Defines the backlog argument of the listen() call. Default is 2000, minimum is 50

* **verbose**

  Defines the verbosity level. Default is 0

* **enable_timers**

  When enabled, some functions trigger an internal timer. To use only for debugging performance. Boolean parameter (0/1) , where 0 is the default (disabled).

* **print_statistics_interval**

  If enable_timers is enabled and verbose >= 10 , a background thread will dump timers information on stderr every *print_statistics_interval* seconds. Default is 60.

* **core_dump_file_size**

  Defines the maximum size of a core dump file, to be used to debug crashes. Default is 0 (no core dump).

 
[mysql] configuration
~~~~~~~~~~~~~~~~~~~~~~

* **mysql_default_schema**

  Each connection *requires* a default schema (database). If a client connects without specifying a schema, mysql_default_schema is applied. It defaults to *information_schema*.

  If you're using mostly one database, specifying a default schema (database) *could* save a request for each new connection.

* **proxy_mysql_port**

  Specifies the port that mysql clients should connect to. It defaults to 6033.

* **mysql_poll_timeout**

  Each connection to proxysql is handled by a thread that call poll() on all the file descriptors opened. poll() is called with a timeout of *mysql_poll_timeout* milliseconds. Default is 10000 (10 seconds) and the minimum is 100 (0.1 seconds).

* **mysql_auto_reconnect_enabled**

  If a connection to mysql server is dropped because killed or timed out, it automatically reconnects. This feature is not completed and should not be enabled. Default is 0 (disabled).

* **mysql_query_cache_enabled**

  Enable the internal MySQL query cache for SELECT statements. Boolean parameter (0/1) , where 1 is the default (enabled).

* **mysql_query_cache_partitions**

  The internal MySQL query cache is divided in several partitions to reduce contentions. By default 16 partitions are created.

* **mysql_max_query_size**

  A query received from a client can be of any length. Although, to optimize memory utilization and to improve performance, only queries with a length smaller than mysql_max_query_size are analyzed and processed. Any query longer then mysql_max_query_size is forwarded to a mysql servers without being processed. That also means that for large queries the query cache is disabled. Default value for mysql_max_query_size is 1048576 (1MB), and the maximum length is 16777210 (few bytes less than 16MB).

* **mysql_max_resultset_size**

  When the server sends a resultset to proxysql, the resultset is stored internally before being forwarded to the client. mysql_max_resultset_size defines the maximum size of a resultset for being buffered: once a resultset passes this threshold it stops the buffering and triggers a fast forward algorithm. Indirectly defines also the maximum size of a cachable resultset. In future a separate option will be introduced. Default is 1048576 (1MB).

* **mysql_query_cache_default_timeout**

  Every cached resultset has a time to live . *mysql_query_cache_default_timeout* defines the default time to live . Defaults is 1 seconds, causing the entries to expire very quickly. It is recommended to increase the *mysql_query_cache_default_timeout* for better performance. Note: in future release will be possible to define what to cache and for how long on a per query basis.

* **mysql_server_version**

  When a client connects to ProxySQL , this introduces itself as mysql version *mysql_server_version* . The default is "5.1.30" ( first GA release of 5.1 ).

* **mysql_usage_user** and **mysql_usage_password**

  At startup (and in future releases also at regular interval), ProxySQL connects to all the MySQL to verify connectivity and the status of read_only to determine if a server is a master or a slave. *mysql_usage_user* and *mysql_usage_password* define the username and password that ProxySQL uses to connect to MySQL. As the name suggests, only USAGE privilege is required. Defaults are *mysql_usage_user=proxy* and *mysql_usage_password=proxy* .

* **mysql_servers**

  Defines a list of mysql servers to use as backend in the format of hostname:port , separated by ';' . Example : mysql_servers=192.168.1.2:3306;192.168.1.3:3306;192.168.1.4:3306 . No default applies.

* **mysql_use_masters_for_reads**

  Implementing read/write split, ProxySQL uses servers where read_only=OFF to send DML statements, while SELECT statements are sent to servers where read_only=ON . If *mysql_use_masters_for_reads* is set to 1, SELECT statements are send also to servers where read_only=OFF . Unless you have servers with read_only=ON it is recommended to always set *mysql_use_masters_for_reads=1* or SELECT statements won't be processed (that is a bug that needs to be fixed). Default is 1 .

* **mysql_connection_pool_enabled**

  ProxySQL implements its own connection pool to MySQL backend. When a connection is assigned to a client it will be used only by that specific client connection and will be never shared. That is: connections to MySQL are not shared among client connections . It connection pool is enabled, when a client disconnects the connections to the backend are reusable by a new connection. Boolean parameter (0/1) , where 1 is the default (enabled).

* **mysql_wait_timeout**

  If connection pool is enabled ( *mysql_connection_pool_enabled=1* ) , unused connection (not assigned to any client) are automatically dropped after *mysql_wait_timeout* seconds. Default is 8 hours , minimum is 1 second .

[mysql users] configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~~

This section includes a list of users and relative password in the form **user=password** . Users without password are in the form **user=** . For example::

  root=secretpass
  webapp=$ecr3t
  guest=
  test=password


Quick start
===========

TODO
