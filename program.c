#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <curl/curl.h>
#include "getopt.h"
#include "cJSON.h"
#include <string.h>
#include <sys/timeb.h>
#include <time.h>

struct Memory {
    char *data;
    size_t size;
};

size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t total = size * nmemb;
    struct Memory *mem = (struct Memory *)userp;

    char *ptr = realloc(mem->data, mem->size + total + 1);
    if (!ptr) return 0;

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, total);
    mem->size += total;
    mem->data[mem->size] = '\0';

    return total;
}

char* read_file(const char* filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *data = malloc(len + 1);
    if (!data) { fclose(f); return NULL; }

    fread(data, 1, len, f);
    data[len] = '\0';
    fclose(f);
    return data;
}

char* get_location() {
    CURL *curl = curl_easy_init();
    CURLcode res;

    struct Memory chunk;
    chunk.data = malloc(1);
    chunk.size = 0;

    if (!chunk.data) return NULL;

    char *country_name = NULL;

    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://ipwho.is/");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);

        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "my-app/1.0");

        res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            cJSON *json = cJSON_Parse(chunk.data);
            if (json) {
                cJSON *country = cJSON_GetObjectItem(json, "country");
                if (country && cJSON_IsString(country)) {
                    country_name = malloc(strlen(country->valuestring) + 1);
                    if (country_name) {
                        strcpy(country_name, country->valuestring);
                    }
                }
                cJSON_Delete(json);
            }
        }
        curl_easy_cleanup(curl);
    }

    free(chunk.data);
    return country_name;
}

double test_download_speed(const char *country, int server_id) {
    char *json_text = read_file("speedtest_server_list.json");
    if (!json_text) {
        printf("Could not read servers.json\n");
        return -1;
    }

    cJSON *servers = cJSON_Parse(json_text);
    free(json_text);

    if (!servers) {
        printf("JSON parse error\n");
        return -1;
    }

    cJSON *server = NULL;
    cJSON *item;
    cJSON_ArrayForEach(item, servers) {
        cJSON *c = cJSON_GetObjectItem(item, "country");
        cJSON *id = cJSON_GetObjectItem(item, "id");

        if (c && cJSON_IsString(c) && id && cJSON_IsNumber(id)) {
            if (strcmp(c->valuestring, country) == 0 && id->valueint == server_id) {
                server = item;
                break;
            }
        }
    }

    if (!server) {
        printf("Server not found for country=%s id=%d\n", country, server_id);
        cJSON_Delete(servers);
        return -1;
    }

    cJSON *host = cJSON_GetObjectItem(server, "host");
    if (!host || !cJSON_IsString(host)) {
        printf("Server host missing\n");
        cJSON_Delete(servers);
        return -1;
    }

    CURL *curl = curl_easy_init();
    struct Memory chunk;
    chunk.data = malloc(1);
    chunk.size = 0;

    double speed_mbps = -1;

    if (curl) {
        char url[512];
        snprintf(url, sizeof(url), "http://%s/download?size=1000000", host->valuestring);
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "speedtester/1.0");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        clock_t start = clock();
        CURLcode res = curl_easy_perform(curl);
        clock_t end = clock();

        if (res == CURLE_OK) {
            double seconds = ((double)(end - start)) / CLOCKS_PER_SEC;
            double bytes = (double)chunk.size;
            speed_mbps = (bytes * 8) / (seconds * 1000 * 1000); // Mbps
            printf("Download speed: %.2f Mbps\n", speed_mbps);
            printf("ID: %d\n", server_id);
            printf("Host: %s\n", cJSON_GetObjectItem(server, "host")->valuestring);
            printf("Location: %s\n", country);
        } else {
            printf("Download failed: %s\n", curl_easy_strerror(res));
        }

        curl_easy_cleanup(curl);
    }

    free(chunk.data);
    cJSON_Delete(servers);

    return speed_mbps;
}

double upload_speed_test(const char *country, int server_id, size_t upload_size_bytes) {
    char *json_text = read_file("speedtest_server_list.json");
    if (!json_text) return -1;

    cJSON *servers = cJSON_Parse(json_text);
    free(json_text);
    if (!servers) return -1;

    cJSON *server = NULL;
    cJSON *item;
    cJSON_ArrayForEach(item, servers) {
        cJSON *c = cJSON_GetObjectItem(item, "country");
        cJSON *id = cJSON_GetObjectItem(item, "id");

        if (c && cJSON_IsString(c) && id && cJSON_IsNumber(id)) {
            if (strcmp(c->valuestring, country) == 0 && id->valueint == server_id) {
                server = item;
                break;
            }
        }
    }

    if (!server) {
        cJSON_Delete(servers);
        return -1;
    }

    cJSON *host_item = cJSON_GetObjectItem(server, "host");
    if (!host_item || !cJSON_IsString(host_item)) {
        cJSON_Delete(servers);
        return -1;
    }

    char url[512];
    snprintf(url, sizeof(url), "http://%s/upload.php", host_item->valuestring);

    CURL *curl = curl_easy_init();
    if (!curl) {
        cJSON_Delete(servers);
        return -1;
    }

    char *upload_data = malloc(upload_size_bytes);
    if (!upload_data) {
        curl_easy_cleanup(curl);
        cJSON_Delete(servers);
        return -1;
    }
    memset(upload_data, 'A', upload_size_bytes);

    struct Memory dummy = {0};

    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dummy);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "speedtester/1.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    clock_t start = clock();
    double total_uploaded = 0;
    CURLcode res = CURLE_OK;

    int iterations = 0;

    while (1) {
        double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;

        if (elapsed >= 15.0) break;

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_READDATA, upload_data);
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)upload_size_bytes);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            printf("Upload failed: %s\n", curl_easy_strerror(res));
            break;
        }

        total_uploaded += upload_size_bytes;
        iterations++;

        if (elapsed >= 1.0 && iterations >= 2) {
            break;
        }
    }

    double total_time = (double)(clock() - start) / CLOCKS_PER_SEC;

    double speed_mbps = 0;
    if (res == CURLE_OK && total_uploaded > 0 && total_time > 0) {
        speed_mbps = (total_uploaded * 8) / (total_time * 1000000.0);
        printf("Upload speed: %.2f Mbps\n", speed_mbps);
        printf("ID: %d\n", server_id);
        printf("Host: %s\n", cJSON_GetObjectItem(server, "host")->valuestring);
        printf("Location: %s\n", country);
    } else {
        speed_mbps = 0;
    }

    free(upload_data);
    curl_easy_cleanup(curl);
    cJSON_Delete(servers);

    return speed_mbps;
}

void test_best_server_for_country() {
    char *country = get_location();
    if (!country) {
        printf("Could not retrieve country for server tests.\n");
        return;
    }

    printf("Finding best server in: %s\n", country);

    char *json_text = read_file("speedtest_server_list.json");
    if (!json_text) {
        free(country);
        return;
    }

    cJSON *servers = cJSON_Parse(json_text);
    free(json_text);
    if (!servers) {
        free(country);
        return;
    }

    clock_t global_start = clock();

    cJSON *item;
    double best_score = -1;
    cJSON *best_server = NULL;
    double best_download = 0, best_upload = 0;

    cJSON_ArrayForEach(item, servers) {

        double elapsed = (double)(clock() - global_start) / CLOCKS_PER_SEC;
        if (elapsed >= 15.0) {
            printf("\nTime limit reached (15s)\n");
            break;
        }

        cJSON *c = cJSON_GetObjectItem(item, "country");
        cJSON *id = cJSON_GetObjectItem(item, "id");
        cJSON *host = cJSON_GetObjectItem(item, "host");

        if (!c || !id || !host ||
            !cJSON_IsString(c) || !cJSON_IsNumber(id) || !cJSON_IsString(host))
            continue;

        if (strcmp(c->valuestring, country) != 0)
            continue;

        printf("Testing server %d...\n", id->valueint);

        char download_url[512];
        char upload_url[512];
        snprintf(download_url, sizeof(download_url),
                 "http://%s/download?size=5000000", host->valuestring);
        snprintf(upload_url, sizeof(upload_url),
                 "http://%s/upload.php", host->valuestring);

        double download_speed = 0;
        CURL *curl = curl_easy_init();
        if (curl) {
            struct Memory chunk = { malloc(1), 0 };

            curl_easy_setopt(curl, CURLOPT_URL, download_url);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &chunk);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L); // short test
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "speedtester/1.0");

            clock_t start = clock();
            CURLcode res = curl_easy_perform(curl);
            clock_t end = clock();

            if (res == CURLE_OK) {
                double seconds = ((double)(end - start)) / CLOCKS_PER_SEC;
                if (seconds > 0)
                    download_speed = (chunk.size * 8) / (seconds * 1000000.0);
            } else {
                download_speed = 0;
            }

            free(chunk.data);
            curl_easy_cleanup(curl);
        }

        double upload_speed = 0;
        size_t upload_size = 2 * 1024 * 1024;
        char *upload_data = malloc(upload_size);

        if (upload_data) {
            memset(upload_data, 'A', upload_size);
            curl = curl_easy_init();

            if (curl) {
                struct Memory dummy = {0};

                curl_easy_setopt(curl, CURLOPT_URL, upload_url);
                curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
                curl_easy_setopt(curl, CURLOPT_READDATA, upload_data);
                curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)upload_size);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L); // short test
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dummy);

                clock_t start = clock();
                CURLcode res = curl_easy_perform(curl);
                clock_t end = clock();

                if (res == CURLE_OK) {
                    double seconds = ((double)(end - start)) / CLOCKS_PER_SEC;
                    if (seconds > 0)
                        upload_speed = (upload_size * 8) / (seconds * 1000000.0);
                } else {
                    upload_speed = 0;
                }

                curl_easy_cleanup(curl);
            }

            free(upload_data);
        }

        double score = download_speed + upload_speed;

        printf(" -> DL: %.2f Mbps | UL: %.2f Mbps\n", download_speed, upload_speed);

        if (score > best_score) {
            best_score = score;
            best_server = item;
            best_download = download_speed;
            best_upload = upload_speed;
        }
    }

    if (best_server) {
        printf("\nBest server found:\n");

        printf("Download speed: %.2f Mbps\n", best_download);
        printf("Upload speed: %.2f Mbps\n", best_upload);
        printf("ID: %d\n", cJSON_GetObjectItem(best_server, "id")->valueint);
        printf("Host: %s\n", cJSON_GetObjectItem(best_server, "host")->valuestring);
        printf("Location: %s\n", country);

    } else {
        printf("No server found.\n");
    }

    cJSON_Delete(servers);
    free(country);
}

int main(int argc, char *argv[])
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
    int opt;
    int n = -1;
    int extra = -1;

    // n: and x: require arguments
    while ((opt = getopt(argc, argv, "n:x:")) != -1) {
        switch (opt) {
            case 'n':
                n = atoi(optarg);
                break;
            case 'x':
                extra = atoi(optarg);
                break;
            case '?':
                printf("Usage: %s -n <1-4> [-x number (server id)]\n", argv[0]);
                return 1;
        }
    }

    // Validate n
    if (n < 1 || n > 4) {
        printf("Error: -n must be between 1 and 4\n");
        return 1;
    }

    // If n = 2-3, extra argument is required
    if (n > 1 && n < 4) {
        if (extra == -1) {
            printf("Error: when -n is 2-3, you must provide -x <number> (server id)\n");
            return 1;
        }
    }

    if (n == 1) {
        char *country = get_location();
        if (country) {
            printf("Your country: %s\n", country);
            free(country);
        } else {
            printf("Could not retrieve country\n");
        }
    }

    if (n == 2) {
        char *country = get_location();
        int server_id = extra;
        test_download_speed(country, server_id);
        free(country);
    }

    if (n == 3) {
        char *country = get_location();
        int server_id = extra;
        size_t size = 3 * 1024 * 1024; // 3 MB
        upload_speed_test(country, server_id, size);
        free(country);
    }

    if (n == 4) {
        test_best_server_for_country();
    }

    curl_global_cleanup();
    return 0;
}
