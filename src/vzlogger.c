/**
 * Main source file
 *
 * @package vzlogger
 * @copyright Copyright (c) 2011, The volkszaehler.org project
 * @license http://www.gnu.org/licenses/gpl.txt GNU Public License
 * @author Steffen Vogel <info@steffenvogel.de>
 */
/*
 * This file is part of volkzaehler.org
 *
 * volkzaehler.org is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * volkzaehler.org is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with volkszaehler.org. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>
#include <signal.h>
#include <getopt.h>
#include <curl/curl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>

#include "list.h"
#include "meter.h"
#include "obis.h"
#include "vzlogger.h"
#include "channel.h"
#include "threads.h"

#ifdef LOCAL_SUPPORT
#include "local.h"
#endif /* LOCAL_SUPPORT */

list_t mappings;	/* mapping between meters and channels */
config_options_t options;	/* global application options */

/**
 * Command line options
 */
const struct option long_options[] = {
	{"config", 	required_argument,	0,	'c'},
	{"log",		required_argument,	0,	'o'},
	{"daemon", 	required_argument,	0,	'd'},
	{"foreground", 	required_argument,	0,	'f'},
#ifdef LOCAL_SUPPORT
	{"httpd", 	no_argument,		0,	'l'},
	{"httpd-port",	required_argument,	0,	'p'},
#endif /* LOCAL_SUPPORT */
	{"verbose",	required_argument,	0,	'v'},
	{"help",	no_argument,		0,	'h'},
	{"version",	no_argument,		0,	'V'},
	{} /* stop condition for iterator */
};

/**
 * Descriptions vor command line options
 */
const char *long_options_descs[] = {
	"configuration file",
	"log file",
	"run as periodically",
	"do not run in background",
#ifdef LOCAL_SUPPORT
	"activate local interface (tiny HTTPd which serves live readings)",
	"TCP port for HTTPd",
#endif /* LOCAL_SUPPORT */
	"enable verbose output",
	"show this help",
	"show version of vzlogger",
	NULL /* stop condition for iterator */
};

/**
 * Print error/debug/info messages to stdout and/or logfile
 *
 * @param id could be NULL for general messages
 * @todo integrate into syslog
 */
void print(log_level_t level, const char *format, void *id, ... ) {
	if (level > options.verbosity) {
		return; /* skip message if its under the verbosity level */
	}

	struct timeval now;
	struct tm * timeinfo;
	char prefix[24];
	size_t pos = 0;

	gettimeofday(&now, NULL);
	timeinfo = localtime(&now.tv_sec);

	/* format timestamp */
	pos += strftime(prefix+pos, 18, "[%b %d %H:%M:%S]", timeinfo);

	/* format section */
	if (id) {
		snprintf(prefix+pos, 8, "[%s]", (char *) id);
	}

	va_list args;
	va_start(args, id);
	/* print to stdout/stderr */
	if (getppid() != 1) { /* running as fork in background? */
		FILE *stream = (level > 0) ? stdout : stderr;

		fprintf(stream, "%-24s", prefix);
		vfprintf(stream, format, args);
		fprintf(stream, "\n");
	}
	va_end(args);

	va_start(args, id);
	/* append to logfile */
	if (options.logfd) {
		fprintf(options.logfd, "%-24s", prefix);
		vfprintf(options.logfd, format, args);
		fprintf(options.logfd, "\n");
		fflush(options.logfd);
	}
	va_end(args);
}

/**
 * Print available options, protocols and OBIS aliases
 */
void show_usage(char *argv[]) {
	const char **desc = long_options_descs;
	const struct option *op = long_options;

	printf("Usage: %s [options]\n\n", argv[0]);

	/* command line options */
	printf("  following options are available:\n");
	while (op->name && desc) {
		printf("\t-%c, --%-12s\t%s\n", op->val, op->name, *desc);
		op++; desc++;
	}

	/* protocols */
	printf("\n  following protocol types are supported:\n");
	for (const meter_details_t *it = meter_get_protocols(); it->name != NULL; it++) {
		printf("\t%-12s\t%s\n", it->name, it->desc);
	}

	/* obis aliases */
	printf("\n  following OBIS aliases are available:\n");
	char obis_str[OBIS_STR_LEN];
	for (const obis_alias_t *it = obis_get_aliases(); it->name != NULL; it++) {
		obis_unparse(it->id, obis_str, OBIS_STR_LEN);
		printf("\t%-17s%-31s%-22s\n", it->name, it->desc, obis_str);
	}

	/* footer */
	printf("\n%s - volkszaehler.org logging utility\n", PACKAGE_STRING);
	printf("by Steffen Vogel <stv0g@0l.de>\n");
	printf("send bugreports to %s\n", PACKAGE_BUGREPORT);
}

/**
 * Fork process to background
 *
 * @link http://www.enderunix.org/docs/eng/daemon.php
 */
void daemonize() {
	if (getppid() == 1) {
		return; /* already a daemon */
	}

	int i = fork();
	if (i < 0) {
		exit(EXIT_FAILURE); /* fork error */
	}
	else if (i > 0) {
		exit(EXIT_SUCCESS); /* parent exits */
	}

	/* child (daemon) continues */
	setsid(); /* obtain a new process group */

	for (i = getdtablesize(); i >= 0; --i) {
		close(i); /* close all descriptors */
	}

	/* handle standart I/O */
	i = open("/dev/null", O_RDWR);
	dup(i);
	dup(i);

	chdir("/"); /* change working directory */
	umask(0022);

	/* ignore signals from parent tty */
	struct sigaction action;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = SIG_IGN;

	sigaction(SIGCHLD, &action, NULL); /* ignore child */
	sigaction(SIGTSTP, &action, NULL); /* ignore tty signals */
	sigaction(SIGTTOU, &action, NULL);
	sigaction(SIGTTIN, &action, NULL);
}

/**
 * Cancel threads
 *
 * Threads gets joined in main()
 */
void quit(int sig) {
	print(log_info, "Closing connections to terminate", NULL);

	foreach(mappings, mapping, map_t) {
		pthread_cancel(mapping->thread);

		foreach(mapping->channels, ch, channel_t) {
			pthread_cancel(ch->thread);
		}
	}
}

/**
 * Parse options from command line
 *
 * @param options pointer to structure for options
 * @return int 0 on succes, <0 on error
 */
int config_parse_cli(int argc, char * argv[], config_options_t * options) {
	while (1) {
		int c = getopt_long(argc, argv, "c:o:p:lhVdfv:", long_options, NULL);

		/* detect the end of the options. */
		if (c == -1) break;

		switch (c) {
			case 'v':
				options->verbosity = atoi(optarg);
				break;

#ifdef LOCAL_SUPPORT
			case 'l':
				options->local = 1;
				break;

			case 'p': /* port for local interface */
				options->port = atoi(optarg);
				break;
#endif /* LOCAL_SUPPORT */

			case 'd':
				options->daemon = 1;
				break;

			case 'f':
				options->foreground = 1;
				break;

			case 'c': /* config file */
				free(options->config);
				options->config = strdup(optarg);
				break;

			case 'o': /* log file */
				options->log = strdup(optarg);
				break;

			case 'V':
				printf("%s\n", VERSION);
				exit(EXIT_SUCCESS);
				break;

			case '?':
			case 'h':
			default:
				show_usage(argv);
				if (c == '?') {
					exit(EXIT_FAILURE);
				}
				else {
					exit(EXIT_SUCCESS);
				}
		}
	}

	return SUCCESS;
}

/**
 * The application entrypoint
 */
int main(int argc, char *argv[]) {
	/* default options */
	options.config = strdup("/etc/vzlogger.conf");
	options.log = NULL;
	options.logfd = NULL;
	options.port = 8080;
	options.verbosity = 0;
	options.comet_timeout = 30;
	options.buffer_length = 600;
	options.retry_pause = 15;
	options.daemon = FALSE;
	options.local = FALSE;
	options.logging = TRUE;

	/* bind signal handler */
	struct sigaction action;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = quit;

	sigaction(SIGINT, &action, NULL);	/* catch ctrl-c from terminal */
	sigaction(SIGHUP, &action, NULL);	/* catch hangup signal */
	sigaction(SIGTERM, &action, NULL);	/* catch kill signal */

	/* initialize ADTs and APIs */
	curl_global_init(CURL_GLOBAL_ALL);
	list_init(&mappings);

	/* parse command line and file options */
	// TODO command line should have a higher priority as file
	if (config_parse_cli(argc, argv, &options) != SUCCESS) {
		return EXIT_FAILURE;
	}

	if (config_parse(options.config, &mappings, &options) != SUCCESS) {
		return EXIT_FAILURE;
	}

	options.logging = (!options.local || options.daemon);

	if (!options.foreground && (options.daemon || options.local)) {
		print(log_info, "Daemonize process...", NULL);
		daemonize();
	}

	/* open logfile */
	if (options.log) {
		FILE *logfd = fopen(options.log, "a");

		if (logfd == NULL) {
			print(log_error, "Cannot open logfile %s: %s", NULL, options.log, strerror(errno));
			return EXIT_FAILURE;
		}

		options.logfd = logfd;
		print(log_debug, "Opened logfile %s", NULL, options.log);
	}

	if (mappings.size <= 0) {
		print(log_error, "No meters found!", NULL);
		return EXIT_FAILURE;
	}

	/* open connection meters & start threads */
	foreach(mappings, mapping, map_t) {
		meter_t *mtr = &mapping->meter;

		if (meter_open(mtr) != SUCCESS) {
			print(log_error, "Failed to open meter. Aborting.", mtr);
			return EXIT_FAILURE;
		}
		else {
			print(log_info, "Meter connection established", mtr);
		}

		pthread_create(&mapping->thread, NULL, &reading_thread, (void *) mapping);
		print(log_debug, "Meter thread started", mtr);

		foreach(mapping->channels, ch, channel_t) {
			/* set buffer length for perriodic meters */
			if (meter_get_details(mtr->protocol)->periodic && options.local) {
				ch->buffer.keep = ceil(options.buffer_length / (double) mapping->meter.interval);
			}

			if (options.logging) {
				pthread_create(&ch->thread, NULL, &logging_thread, (void *) ch);
				print(log_debug, "Logging thread started", ch);
			}
		}
	}

#ifdef LOCAL_SUPPORT
	 /* start webserver for local interface */
	struct MHD_Daemon *httpd_handle = NULL;
	if (options.local) {
		print(log_info, "Starting local interface HTTPd on port %i", "http", options.port);
		httpd_handle = MHD_start_daemon(
			MHD_USE_THREAD_PER_CONNECTION,
			options.port,
			NULL, NULL,
			&handle_request, &mappings,
			MHD_OPTION_END
		);
	}
#endif /* LOCAL_SUPPORT */

	/* wait for all threads to terminate */
	foreach(mappings, mapping, map_t) {
		meter_t *mtr = &mapping->meter;

		pthread_join(mapping->thread, NULL);

		foreach(mapping->channels, ch, channel_t) {
			pthread_join(ch->thread, NULL);

			channel_free(ch);
		}

		meter_close(mtr); /* closing connection */

		list_free(&mapping->channels);
		meter_free(mtr);
	}

#ifdef LOCAL_SUPPORT
	/* stop webserver */
	if (httpd_handle) {
		MHD_stop_daemon(httpd_handle);
	}
#endif /* LOCAL_SUPPORT */

	/* householding */
	free(options.config);
	list_free(&mappings);
	curl_global_cleanup();

	/* close logfile */
	if (options.logfd) {
		free(options.log);
		fclose(options.logfd);
	}

	return EXIT_SUCCESS;
}
