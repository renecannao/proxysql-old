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

* libssl and libssl-dev
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

  Specify the stack size used by every thread created in proxysql , in bytes . Default is 524288 ( 512KB ) , minimum is 65536 ( 64KB ) , and no maximum is defined.

  The default stack_size in Linux is 8MB. Starting hundreds of connections/threads will quickly eat all memory so we need to lower this down to be more memory efficient.

  Latest versions of ProxySQL use threads pool instead of one thread per connection, therefore now the stack size has little memory footprint.

* **net_buffer_size**

  Each connection to proxysql creates a so called MySQL data stream. Each MySQL data stream has 2 buffers for recv and send. *net_buffer_size* defines the size of each of these buffers. Each connection from proxysql to a mysql server needs a MySQL data stream. Each client connection can have a different number of MySQL data stream associated to it:

  - 1 : The client connects to proxysql and this one is able to serve each request from its own cache. No connections are established to mysql server.

  - 2 : The client connects to proxysql and this one needs to connect to a mysql server to serve requests from client.

  - 3 : The client connects to proxysql and this one needs to connect to two mysql servers, a master and a slave.

  That means that each client connection needs 1, 2 or 3 MySQL data streams, for a total of 2, 4 or 6 network buffers. Increasing this variables boost performance in case of large dataset, at the cost of additional memory usage. Default is 8192 (8KB), minimum is 1024 (1KB), and no maximum is defined.

* **proxy_admin_port**

  It defines the administrative port for runtime configuration and statistics.

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

  Every cached resultset has a time to live . *mysql_query_cache_default_timeout* defines the default time to live in case a TTL is not specified for a specific query pattern. Defaults is 1 seconds, causing the entries to expire very quickly. It is recommended to increase the *mysql_query_cache_default_timeout* for better performance. *mysql_query_cache_default_timeout*=0 disables caching for any query not explicity 

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

* **mysql_socket**

  ProxySQL can accept connection also through the Unix Domain socket specified in *mysql_socket* . This socket is usable only if the client and ProxySQL are running on the same server. Benchmark shows that with workload where all the queries are served from the internal query cache, Unix Domain socket provides 50% more throughput than TCP socket. Default is */tmp/proxysql.sock*

* **mysql_threads**

  Early versions of ProxySQL used 1 thread per connection, while recent versions use a pool of threads that handle all the connections. Performance improved by 20% for certain workload and an optimized number of threads. Further optimizations are expected. Default is *number-of-CPU-cores X 2* , minimum is 2 and maximum is 128 .

[mysql users] configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~~

This section includes a list of users and relative password in the form **user=password** . Users without password are in the form **user=** . For example::

  root=secretpass
  webapp=$ecr3t
  guest=
  test=password


Quick start Tutorial
====================

Download and compile
~~~~~~~~~~~~~~~~~~~~

These are the simple steps to download and compile ProxySQL::
 
  rene@voyager:~$ mkdir proxysql
  rene@voyager:~$ cd proxysql
  rene@voyager:~/proxysql$ wget -q https://github.com/renecannao/proxysql/archive/master.zip -O proxysql.zip
  rene@voyager:~/proxysql$ unzip -q proxysql.zip 
  rene@voyager:~/proxysql$ cd proxysql-master/src/
  rene@voyager:~/proxysql/proxysql-master/src$ mkdir obj
  rene@voyager:~/proxysql/proxysql-master/src$ make
  gcc -c -o obj/main.o main.c -I../include -lpthread -lpcre -ggdb -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags glib-2.0` -DPKTALLOC -O2
  gcc -c -o obj/free_pkts.o free_pkts.c -I../include -lpthread -lpcre -ggdb -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags glib-2.0` -DPKTALLOC -O2
  gcc -c -o obj/mem.o mem.c -I../include -lpthread -lpcre -ggdb -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags glib-2.0` -DPKTALLOC -O2
  gcc -c -o obj/debug.o debug.c -I../include -lpthread -lpcre -ggdb -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags glib-2.0` -DPKTALLOC -O2
  gcc -c -o obj/fundadb_hash.o fundadb_hash.c -I../include -lpthread -lpcre -ggdb -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags glib-2.0` -DPKTALLOC -O2
  gcc -c -o obj/global_variables.o global_variables.c -I../include -lpthread -lpcre -ggdb -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags glib-2.0` -DPKTALLOC -O2
  gcc -c -o obj/mysql_connpool.o mysql_connpool.c -I../include -lpthread -lpcre -ggdb -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags glib-2.0` -DPKTALLOC -O2
  gcc -c -o obj/mysql_protocol.o mysql_protocol.c -I../include -lpthread -lpcre -ggdb -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags glib-2.0` -DPKTALLOC -O2
  gcc -c -o obj/mysql_handler.o mysql_handler.c -I../include -lpthread -lpcre -ggdb -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags glib-2.0` -DPKTALLOC -O2
  gcc -c -o obj/network.o network.c -I../include -lpthread -lpcre -ggdb -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags glib-2.0` -DPKTALLOC -O2
  gcc -c -o obj/queue.o queue.c -I../include -lpthread -lpcre -ggdb -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags glib-2.0` -DPKTALLOC -O2
  gcc -c -o obj/threads.o threads.c -I../include -lpthread -lpcre -ggdb -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags glib-2.0` -DPKTALLOC -O2
  gcc -o proxysql obj/main.o obj/free_pkts.o obj/mem.o obj/debug.o obj/fundadb_hash.o obj/global_variables.o obj/mysql_connpool.o obj/mysql_protocol.o obj/mysql_handler.o obj/network.o obj/queue.o obj/threads.o -I../include -lpthread -lpcre -ggdb -rdynamic -lcrypto `mysql_config --libs_r --cflags` `pkg-config --libs --cflags glib-2.0` -DPKTALLOC -O2 -lm

Congratulations! You have just compiled proxysql!

Create a small replication environment
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To try proxysql we can use a standalone mysqld instance, or a small replication cluster for better testing. To quickly create a small replication environment you can use MySQL Sandbox::
  
  rene@voyager:~$ make_replication_sandbox mysql_binaries/mysql-5.5.34-linux2.6-i686.tar.gz 
  installing and starting master
  installing slave 1
  installing slave 2
  starting slave 1
  .... sandbox server started
  starting slave 2
  .... sandbox server started
  initializing slave 1
  initializing slave 2
  replication directory installed in $HOME/sandboxes/rsandbox_mysql-5_5_34


Now that the cluster is installed, verify on which ports are listening the various mysqld processes::
  
  rene@voyager:~$ cd sandboxes/rsandbox_mysql-5_5_34
  rene@voyager:~/sandboxes/rsandbox_mysql-5_5_34$ cat default_connection.json 
  {
  "master":  
      {
          "host":     "127.0.0.1",
          "port":     "23389",
          "socket":   "/tmp/mysql_sandbox23389.sock",
          "username": "msandbox@127.%",
          "password": "msandbox"
      }
  ,
  "node1":  
      {
          "host":     "127.0.0.1",
          "port":     "23390",
          "socket":   "/tmp/mysql_sandbox23390.sock",
          "username": "msandbox@127.%",
          "password": "msandbox"
      }
  ,
  "node2":  
      {
          "host":     "127.0.0.1",
          "port":     "23391",
          "socket":   "/tmp/mysql_sandbox23391.sock",
          "username": "msandbox@127.%",
          "password": "msandbox"
      }
  }

The mysqld processes are listening on port 23389 (master) and 23390 and 23391 (slaves).

Configure ProxySQL
~~~~~~~~~~~~~~~~~~

ProxySQL come with an example configuration file, that may not work for your setup. Remove it and create a new one::
  
  vegaicm@voyager:~/proxysql/proxysql-master/src$ rm proxysql.cnf 
  vegaicm@voyager:~/proxysql/proxysql-master/src$ cat > proxysql.cnf << EOF
  > [global]
  > [mysql]
  > mysql_usage_user=proxy
  > mysql_usage_password=proxy
  > mysql_servers=127.0.0.1:23389;127.0.0.1:23390;127.0.0.1:23391
  > mysql_default_schema=information_schema
  > mysql_connection_pool_enabled=1
  > mysql_max_resultset_size=1048576
  > mysql_max_query_size=1048576
  > mysql_query_cache_enabled=1
  > mysql_query_cache_partitions=16
  > mysql_query_cache_default_timeout=30
  > [mysql users]
  > msandbox=msandbox
  > test=password
  > EOF

Note the *[global]* section: it is mandatory even if unused.

Create users on MySQL
~~~~~~~~~~~~~~~~~~~~~

We configured ProxySQL to use 3 users:

* proxy : this user needs only USAGE privileges, and it is used to verify that the server is alive and the value of read_only
* msandbox and test : these are two normal users that application can use to connect to mysqld through the proxy

User msandbox is already there, so only users proxy and test needs to be created. For example::

  rene@voyager:~$ mysql -h 127.0.0.1 -u root -pmsandbox -P23389 -e "GRANT USAGE ON *.* TO 'proxy'@'127.0.0.1' IDENTIFIED BY 'proxy'";
  rene@voyager:~$ mysql -h 127.0.0.1 -u root -pmsandbox -P23389 -e "GRANT ALL PRIVILEGES ON *.* TO 'test'@'127.0.0.1' IDENTIFIED BY 'password'";

Configure the slaves with read_only=0
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

ProxySQL distinguish masters from slaves only checking the global variables read_only. This means that you *must* configure the slaves with read_only=ON or ProxySQL will send DML to them as well. Note that this make ProxySQL suitable for multi-master environments using clustering solution like NDB and Galera.

Verify the status of read_only on all servers::
  
  rene@voyager:~$ for p in 23389 23390 23391 ; do mysql -h 127.0.0.1 -u root -pmsandbox -P$p -B -N -e "SHOW VARIABLES LIKE 'read_only'" ; done
  read_only	OFF
  read_only	OFF
  read_only	OFF

Change read_only on slaves::
  
  rene@voyager:~$ for p in 23390 23391 ; do mysql -h 127.0.0.1 -u root -pmsandbox -P$p -B -N -e "SET GLOBAL read_only=ON" ; done


Verify again the status of read_only on all servers::
  
  rene@voyager:~$ for p in 23389 23390 23391 ; do mysql -h 127.0.0.1 -u root -pmsandbox -P$p -B -N -e "SHOW VARIABLES LIKE 'read_only'" ; done
  read_only	OFF
  read_only	ON
  read_only	ON

Start ProxySQL
~~~~~~~~~~~~~~

ProxySQL is now ready to be executed. Please note that currently it run only on foreground and it does not daemonize::
  
  rene@voyager:~/proxysql/proxysql-master/src$ ./proxysql 
  Server 127.0.0.1 port 23389
  server 127.0.0.1 read_only OFF
  Server 127.0.0.1 port 23390
  server 127.0.0.1 read_only ON
  Server 127.0.0.1 port 23391
  server 127.0.0.1 read_only ON


Connect to ProxySQL
~~~~~~~~~~~~~~~~~~~

You can now connect to ProxySQL running any mysql client. For example::
  
  rene@voyager:~$ mysql -u msandbox -pmsandbox -h 127.0.0.1 -P6033
  Welcome to the MySQL monitor.  Commands end with ; or \g.
  Your MySQL connection id is 3060194112
  Server version: 5.1.30 MySQL Community Server (GPL)
  
  Copyright (c) 2000, 2013, Oracle and/or its affiliates. All rights reserved.
  
  Oracle is a registered trademark of Oracle Corporation and/or its
  affiliates. Other names may be trademarks of their respective
  owners.
  
  Type 'help;' or '\h' for help. Type '\c' to clear the current input statement.
  
  mysql> 

An acute observer can immediately understand that we aren't connected directly to MySQL, but to ProxySQL . A less acute observer can probably understand it from the next output::
  
  mysql> \s
  --------------
  mysql  Ver 14.14 Distrib 5.5.34, for debian-linux-gnu (i686) using readline 6.2
  
  Connection id:		3060194112
  Current database:	information_schema
  Current user:		msandbox@localhost
  SSL:			Not in use
  Current pager:		stdout
  Using outfile:		''
  Using delimiter:	;
  Server version:		5.1.30 MySQL Community Server (GPL)
  Protocol version:	10
  Connection:		127.0.0.1 via TCP/IP
  Server characterset:	latin1
  Db     characterset:	utf8
  Client characterset:	latin1
  Conn.  characterset:	latin1
  TCP port:		6033
  Uptime:			51 min 56 sec
  
  Threads: 4  Questions: 342  Slow queries: 0  Opens: 70  Flush tables: 1  Open tables: 63  Queries per second avg: 0.109
  --------------
  
  mysql>

Did you notice it now? If not, note that line::
  
  Server version:       5.1.30 MySQL Community Server (GPL)

We installed MySQL 5.5.34 , but the client says 5.1.30 . This because during the authentication phase ProxySQL introduces itself as MySQL version 5.1.30 . This is configurable via parameter *mysql_server_version* . Note: ProxySQL doesn't use the real version of the backends because it is possible to run backends with different versions.

Additionally, mysql says that the current database is *information_schema* while we didn't specify any during the connection.

On which server are we connected now? Because of read/write split, it is not always possible to answer this question.
What we know is that:

* SELECT statements without FOR UPDATE are sent to the slaves ( and also to the master if *mysql_use_masters_for_reads=1* , by default ) ;
* SELECT statements with FOR UPDATE are sent to a master ;
* any other statement is sent to the master only ;
* SELECT statements without FOR UPDATE are cached .

Let try to understand to which server are we connected running the follow::
  
  mysql> SELECT @@port;
  +--------+
  | @@port |
  +--------+
  |  23391 |
  +--------+
  1 row in set (0.00 sec)

We are connected on server using port 23391 . This information is true only the *first* time we run it. In fact, if we run the same query from another connection we will get the same result because this query is cached.
Also, if we disconnect the client and reconnect again, the above query will return the same result also after the cache is invalidated. Why? ProxySQL implement connection pooling, and a if a client connection to the proxy is close the backend connection will be reused by the next client connection.

To verify the effect of the cache, it is enough to run the follow commands::
  
  mysql> SELECT NOW();
  +---------------------+
  | NOW()               |
  +---------------------+
  | 2013-11-20 17:55:25 |
  +---------------------+
  1 row in set (0.00 sec)
  
  mysql> SELECT @@port;
  +--------+
  | @@port |
  +--------+
  |  23391 |
  +--------+
  1 row in set (0.00 sec)
  
  mysql> SELECT NOW();
  +---------------------+
  | NOW()               |
  +---------------------+
  | 2013-11-20 17:55:25 |
  +---------------------+
  1 row in set (0.00 sec)

The resultset of "SELECT NOW()" doesn't change with time. Probably this is not what you want.

Testing R/W split
~~~~~~~~~~~~~~~~~

The follow is an example of how to test R/W split .

Write on master::
  
  mysql> show databases;
  +--------------------+
  | Database           |
  +--------------------+
  | information_schema |
  | mysql              |
  | performance_schema |
  | test               |
  +--------------------+
  4 rows in set (0.02 sec)
  
  mysql> use test
  Database changed
  mysql> CREATE table tbl1 (id int);
  Query OK, 0 rows affected (0.25 sec)
  
  mysql> insert into tbl1 values (1);
  Query OK, 1 row affected (0.03 sec)

Read from a slave::
 
  mysql> SELECT * FROM tbl1;
  +------+
  | id   |
  +------+
  |    1 |
  +------+
  1 row in set (0.00 sec)

The follow query retrieves also @@port, so we can verify it is executed on a slave::

  mysql> SELECT @@port, t.* FROM tbl1 t;
  +--------+------+
  | @@port | id   |
  +--------+------+
  |  23391 |    1 |
  +--------+------+
  1 row in set (0.00 sec)

To force a read from master, we must specify FOR UPDATE::

  mysql> SELECT @@port, t.* FROM tbl1 t FOR UPDATE;
  +--------+------+
  | @@port | id   |
  +--------+------+
  |  23389 |    1 |
  +--------+------+
  1 row in set (0.01 sec)


