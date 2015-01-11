/*
 * proft.c
 *	Test profiling hooks
 */
#include <sys/types.h>
#include <sys/sched.h>
#include <stdlib.h>

static uint nsamples = 0;
static uint *samples, samplesize;
extern uchar __main[], _etext[];

static void
ticktock(char *evstr)
{
	uint eip = *(uint *)(evstr + EVLEN), idx;

	nsamples += 1;
	idx = (eip - (uint)__main);
	if (idx < samplesize) {
		samples[idx] += 1;
	}
}

static void
spin(void)
{
	volatile int y;

	for (y = 0; y < 100000000; ++y) {
		;
	}
}

static void
dump_samples(void)
{
	int x;

	for (x = 0; x < samplesize; ++x) {
		if (samples[x] == 0) {
			continue;
		}
		printf("0x%x: %d\n", (uint)__main + x, samples[x]);
	}
}

int
main()
{
	int x;

	samplesize = _etext - __main;
	samples = malloc(sizeof(uint) * samplesize);
	handle_event("tick", ticktock);
	printf("Turn on profile interrupts\n");
	sched_op(SCHEDOP_PROFILE, 1);
	for (x = 0; x < 5; ++x) {
		spin();
		printf("samples = %d\n", nsamples);
	}
	printf("Turn off profile interrupts\n");
	sched_op(SCHEDOP_PROFILE, 0);
	for (x = 0; x < 2; ++x) {
		spin();
		printf("samples = %d\n", nsamples);
	}
	dump_samples();
	return(0);
}
