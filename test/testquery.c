#include "../src/mapper_internal.h"
#include <mapper/mapper.h>
#include <stdio.h>
#include <math.h>
#include <lo/lo.h>

#include <unistd.h>
#include <signal.h>

#ifdef WIN32
#define usleep(x) Sleep(x/1000)
#endif

int automate = 1;

mapper_device source = 0;
mapper_device destination = 0;
mapper_signal sendsig[4] = {0, 0, 0, 0};
mapper_signal recvsig[4] = {0, 0, 0, 0};

int sent = 0;
int received = 0;
int done = 0;

void query_response_handler(mapper_signal sig, mapper_db_signal props,
                            int instance_id, void *value, int count,
                            mapper_timetag_t *timetag)
{
    if (value) {
        printf("--> source got query response: %s %i\n", props->name, (*(int*)value));
    }
    else {
        printf("--> source got empty query response: %s\n", props->name);
    }

    received++;
}

/*! Creation of a local source. */
int setup_source()
{
    char sig_name[20];
    source = mdev_new("testquery-send", 0, 0);
    if (!source)
        goto error;
    printf("source created.\n");

    int mn=0, mx=10;

    for (int i = 0; i < 4; i++) {
        snprintf(sig_name, 20, "%s%i", "/outsig_", i);
        sendsig[i] = mdev_add_output(source, sig_name, 1, 'i', 0, &mn, &mx);
        msig_set_callback(sendsig[i], query_response_handler, 0);
    }

    printf("Output signals registered.\n");
    printf("Number of outputs: %d\n", mdev_num_outputs(source));

    return 0;

error:
    return 1;
}

void cleanup_source()
{
    if (source) {
        if (source->routers) {
            printf("Removing router.. ");
            fflush(stdout);
            mdev_remove_router(source, source->routers);
            printf("ok\n");
        }
        printf("Freeing source.. ");
        fflush(stdout);
        mdev_free(source);
        printf("ok\n");
    }
}

void insig_handler(mapper_signal sig,mapper_db_signal props,
                   int instance_id, void *value, int count,
                   mapper_timetag_t *timetag)
{
    if (value) {
        printf("--> destination got %s %f\n", props->name, (*(float*)value));
    }
    received++;
}

/*! Creation of a local destination. */
int setup_destination()
{
    char sig_name[10];
    destination = mdev_new("testquery-recv", 0, 0);
    if (!destination)
        goto error;
    printf("destination created.\n");

    float mn=0, mx=1;

    for (int i = 0; i < 4; i++) {
        snprintf(sig_name, 10, "%s%i", "/insig_", i);
        recvsig[i] = mdev_add_input(destination, sig_name, 1,
                                    'f', 0, &mn, &mx, insig_handler, 0);
    }

    printf("Input signal /insig registered.\n");
    printf("Number of inputs: %d\n", mdev_num_inputs(destination));

    return 0;

error:
    return 1;
}

void cleanup_destination()
{
    if (destination) {
        printf("Freeing destination.. ");
        fflush(stdout);
        mdev_free(destination);
        printf("ok\n");
    }
}



void wait_local_devices()
{
    while (!(mdev_ready(source) && mdev_ready(destination))) {
        mdev_poll(source, 0);
        mdev_poll(destination, 0);

        usleep(50 * 1000);
    }
}

void loop()
{
    printf("-------------------- GO ! --------------------\n");
    int i = 10, j = 0, count;

    if (automate) {
        mapper_monitor mon = mapper_monitor_new(source->admin, 0);

        char src_name[1024], dest_name[1024];
        mapper_monitor_link(mon, mdev_name(source),
                            mdev_name(destination), 0, 0);

        for (int i = 0; i < 4; i++) {
            msig_full_name(sendsig[i], src_name, 1024);
            msig_full_name(recvsig[i], dest_name, 1024);
            mapper_monitor_connect(mon, src_name, dest_name, 0, 0);
        }

        // wait until connection has been established
        while (!source->routers || !source->routers->n_connections) {
            mdev_poll(source, 1);
            mdev_poll(destination, 1);
        }

        mapper_monitor_free(mon);
    }

    while (i >= 0 && !done) {
        for (j = 0; j < 2; j++) {
            msig_update_float(recvsig[j], ((i % 10) * 1.0f));
        }
        printf("\ndestination values updated to %f -->\n", (i % 10) * 1.0f);
        for (j = 0; j < 4; j++) {
            count = msig_query_remotes(sendsig[j], MAPPER_NOW);
            printf("Sent %i queries for sendsig[%i]\n", count, j);
        }
        mdev_poll(destination, 200);
        mdev_poll(source, 200);
        i--;
    }
}

void ctrlc(int sig)
{
    done = 1;
}

int main()
{
    int result = 0;

    signal(SIGINT, ctrlc);

    if (setup_destination()) {
        printf("Error initializing destination.\n");
        result = 1;
        goto done;
    }

    if (setup_source()) {
        printf("Done initializing source.\n");
        result = 1;
        goto done;
    }

    wait_local_devices();

    loop();

done:
    cleanup_destination();
    cleanup_source();
    return result;
}
