#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <wiringPi.h>

#define FIRST_SLEEP_SECONDS	8
#define MINIMUM_FREE_MBYTES	500

#define REC_DIR			"/root/records"
#define RPIVID_PATH		"/bin/raspivid"

/*
 * Timevals are not accessed in mutual exclusion as this is not really useful :
 * start_tv will be used in read-only by threads, and last_tv potential
 * inconsistence would result in a close-to-nothing video shortening
 */
static struct timeval start_tv;
static volatile struct timeval last_tv;

/* Interrupt threads have to access this "boolean" in mutual exclusion */
static volatile unsigned short is_record_on = 0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/* A strcmp() wrapper used as a comparison function by qsort() */
static int strcmp_wrapper(const void * str_a, const void * str_b)
{
	return strcmp(*(const char **)str_a, *(const char **)str_b);
}

void ensure_free_space(void)
{
	DIR *directory;
	char **filenames;
	struct dirent *entry;
	struct statvfs svfs;
	int i = 0, file_count = 0;

	if ((directory = opendir(REC_DIR)) == NULL)
		return;

	/* returns if free space is already sufficient */
	statvfs("/", &svfs);
	if ((svfs.f_bfree * svfs.f_bsize / 1000000) > MINIMUM_FREE_MBYTES)
		return;

	/* let's build an alphabetically ordered file list */
	while ((entry = readdir(directory)) != NULL)
		if((entry->d_name)[0] != '.')
			++file_count;

	if ((filenames = malloc(file_count * sizeof(const char *))) == NULL)
		return;

	rewinddir(directory);
	while ((entry = readdir(directory)) != NULL)
		if((entry->d_name)[0] != '.')
			filenames[i++] = entry->d_name;

	qsort(filenames, file_count, sizeof(const char *), strcmp_wrapper);

	/* remove files as long as there is not enough free space */
	i = 0;
	do {
		remove(filenames[i++]);
		statvfs("/", &svfs);
	} while (i < file_count && (svfs.f_bfree * svfs.f_bsize / 1000000) < MINIMUM_FREE_MBYTES);

	free(filenames);
}

void motion_handler(void)
{
	pid_t pid;
	unsigned short should_record, day, hour, minute;
	unsigned long sleep_us;
	struct timeval diff_tv, used_tv;

	char out_filename [strlen(REC_DIR)+22];
	char *const util_params [] = {"raspivid", "-t", "0", "-n", "-fps", "24", 
	 "-o", out_filename, NULL};

	/* save the last time a motion has been detected, regardless of recording state */
	gettimeofday(&used_tv, NULL);
	last_tv = used_tv;

	/* should we start a new recording ? */
	pthread_mutex_lock(&mutex);
	should_record = !is_record_on;
	is_record_on = 1;
	pthread_mutex_unlock(&mutex);

	if (!should_record)
		return;

	pid = fork();

	if (pid == 0) {
		/* child process, used to launch the recording program */
		timersub(&used_tv, &start_tv, &diff_tv);

		day = diff_tv.tv_sec / (24 * 3600);
		hour = (diff_tv.tv_sec / 3600) % 24;
		minute = (diff_tv.tv_sec / 60) % 60;
		sprintf(out_filename, "%s/rec_%02d_%02d-%02d-%02d.h264", REC_DIR,
		 day+1, hour, minute, diff_tv.tv_sec % 60);

		execv(RPIVID_PATH, util_params);
		/* if execv returned, something bad happened */
		printf("execv error : %s\n", strerror(errno));
	}
	else {
		/* sleep a minimum time plus extra for motions in the meantime */
		sleep(FIRST_SLEEP_SECONDS);

		sleep_us = 0;
		do {
			usleep(sleep_us);

			timersub(&last_tv, &used_tv, &diff_tv);
			used_tv = last_tv;
			sleep_us = diff_tv.tv_sec * 1000000 + diff_tv.tv_usec;
		} while (sleep_us != 0);

		/* time to stop the record by sending a signal to the child process */
		kill(pid, SIGINT);

		pthread_mutex_lock(&mutex);
		is_record_on = 0;
		pthread_mutex_unlock(&mutex);

		/* filesystem sanity check */
		sync();
		ensure_free_space();
	}
}

int main(int argc, char **argv)
{
	int rc;

	/* ensure directory exists, no permissions needed in single user mode */
	if ((rc = mkdir(REC_DIR, S_IRWXU|S_IRWXG|S_IRWXO)) != 0 && errno != EEXIST)
		return rc;

	/* this global variable will be used later to create relative timestamps */
	gettimeofday(&start_tv, NULL);

	wiringPiSetup();

	/* BCM GPIO 17 ISR */
	wiringPiISR(0, INT_EDGE_RISING, &motion_handler);
	/* BCM GPIO 22 ISR */
	wiringPiISR(3, INT_EDGE_RISING, &motion_handler);
	/* BCM GPIO 23 ISR */
	wiringPiISR(4, INT_EDGE_RISING, &motion_handler);
	/* BCM GPIO 24 ISR */
	wiringPiISR(5, INT_EDGE_RISING, &motion_handler);

	while (1)
		sleep(3600);

	return 0;
}
