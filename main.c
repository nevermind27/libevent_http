#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <signal.h>
#include <pthread.h>
#include <stdlib.h>
#include <stdbool.h>
#include <curl/curl.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/listener.h>
#include <event2/util.h>
#include <event2/event.h>
#include <event2/http.h>
#include <event2/keyvalq_struct.h>

static const int TIMEOUT = 20;

struct RequestData {
    const char* url;
    double total_time;
};

void* request_thread(void* arg) {
    struct RequestData* data = (struct RequestData*)arg;

    CURL* curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, false); 
        curl_easy_setopt(curl, CURLOPT_URL, data->url);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, TIMEOUT);
        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &data->total_time);
            printf("Request to %s successful\n", data->url);
        } else {
            fprintf(stderr, "Failed to request %s: %s\n", data->url, curl_easy_strerror(res));
        }

        curl_easy_cleanup(curl);
    } else {
        fprintf(stderr, "Failed to initialize CURL\n");
    }
    pthread_exit(NULL);
}

static void request_handler(struct evhttp_request *req, void *arg)
{
    struct evhttp_uri *uri = evhttp_uri_parse(evhttp_request_get_uri(req));
    const char *path = evhttp_uri_get_path(uri);
    const char *query = evhttp_uri_get_query(uri);

    struct RequestData* data[3];
    pthread_t threads[3];

    for (int i = 0; i < 3; i++) {
        data[i] = (struct RequestData*)malloc(sizeof(struct RequestData));
        data[i]->url =  "https://demo-test-task-delayed.fly.dev/data";
      
        if (pthread_create(&threads[i], NULL, request_thread, data[i]) != 0) {
            fprintf(stderr, "Failed to create thread for request\n");
        }
    }

    struct evbuffer *output = evhttp_request_get_output_buffer(req);
    for (int i = 0; i < 3; i++) {
        pthread_join(threads[i], NULL);

        if (i == 0) {
            evbuffer_add_printf(output, "{\"timeouts\":[%f,%f,%f]}\n", data[0]->total_time, data[1]->total_time, data[2]->total_time);
            evhttp_send_reply(req, HTTP_OK, "OK", output);
        }
        free(data[i]);
    }

    evhttp_uri_free(uri);
}

int main()
{
    struct event_base *base;
    struct evhttp *http;
    struct evhttp_bound_socket *handle;

    base = event_base_new();
    if (!base) {
        fprintf(stderr, "Could not initialize libevent!\n");
        return 1;
    }

    http = evhttp_new(base);
    if (!http) {
        fprintf(stderr, "Could not create HTTP server!\n");
        return 1;
    }

    handle = evhttp_bind_socket_with_handle(http, "0.0.0.0", 3000);
    if (!handle) {
        fprintf(stderr, "Could not bind to port 3000!\n");
        return 1;
    }

    evhttp_set_gencb(http, request_handler, NULL);

    event_base_dispatch(base);

    evhttp_free(http);
    event_base_free(base);

    printf("done\n");
    return 0;
}
