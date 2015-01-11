/*
 * mcount.c
 *	Hooks to gather histograms of program counter values
 */
#include <sys/types.h>
#include <sys/sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <mcount.h>

static ulong nsamples, strays,
	*samples, samplesize;
static uint granularity;
static time_t started, finished;
extern uchar __main[], _etext[];

/*
 * ticktock()
 *	Handler for "tick" time event
 */
static void
ticktock(char *evstr)
{
	uint eip = *(uint *)(evstr + EVLEN), idx;

	nsamples += 1;
	idx = (eip - (uint)__main) / granularity;
	if (idx < samplesize) {
		samples[idx] += 1;
	} else {
		strays += 1;
	}
}

/*
 * dump_samples()
 *	Write out sampling results to "prof.out"
 */
void
dump_samples(void)
{
	int x;
	FILE *fp;
	char *startp, *finp;

	/*
	 * Shut down the profiling
	 */
	sched_op(SCHEDOP_PROFILE, 0);
	(void)time(&finished);

	/*
	 * Create the file
	 */
	fp = fopen("prof.out", "w");
	if (fp == NULL) {
		return;
	}

	/*
	 * Print out a prolog
	 */
	startp = strdup(ctime(&started));
	finp = strdup(ctime(&finished));
	fprintf(fp, "Gathered from %s to %s\n", startp, finp);
	free(startp); free(finp);
	fprintf(fp, "base: 0x%x top: 0x%x granularity: %u"
	 " samples: %lu strays: %lu\n",
		(uint)__main, (uint)_etext, granularity, nsamples, strays);

	/*
	 * Now dump each slot which is non-zero
	 */
	for (x = 0; x < samplesize; ++x) {
		if (samples[x] == 0) {
			continue;
		}
		fprintf(fp, "0x%x: %ld\n",
			(uint)__main + x*granularity, samples[x]);
	}

	/*
	 * Done with the file
	 */
	fclose(fp);

	/*
	 * Free sample buffer
	 */
	free(samples);
	samples = NULL;
}

/*
 * take_samples()
 *	Set up to gather histogram, and start the interval interrupts
 */
void
take_samples(uint g)
{
	/*
	 * Build an array of the needed size
	 */
	granularity = g;
	samplesize = (_etext - __main) / granularity;
	samples = malloc(sizeof(uint) * samplesize);
	(void)time(&started);
	handle_event("tick", ticktock);
	sched_op(SCHEDOP_PROFILE, 1);
}
