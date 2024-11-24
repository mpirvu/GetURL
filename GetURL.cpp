/*
 * Application for sending http requests to a particular URL, continuously.
 * It uses the cURL library.
 */
#include <stdio.h>
#include <curl/curl.h>
#include <assert.h>
#include <thread>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> // for getopt
#include <sys/time.h>
#include <chrono>
#include <vector>
#include <iostream>
#include <exception>

/* must link with the curl library */


static const int MAX_THREADS = 100;


typedef struct threadData
{
   unsigned long long client_id;
   unsigned long long num_pages;
   unsigned long long num_bytes;
   unsigned long long num_errors;
   unsigned long long cumm_resp_time;
   char padding[128-5*sizeof(unsigned long long)]; // to minimize ping-pong effects
}thread_data_t;


typedef struct global_data
{
   int num_threads;      // how many threads will be created
   unsigned num_iter;    // how many times each thread will access the website
   unsigned remainingTestDurationSec; // duration of test;
   unsigned think_time;  // delay between requests; default is 0
   unsigned sec_periodical_stats; // how often we should print preliminary statistics (seconds)
   unsigned max_errors_allowed_per_client;
   char *url;            // the url to access
}global_data_t;


// global variables
thread_data_t tld[MAX_THREADS]; // one private buffer for each thread
global_data_t globalData;
volatile long finishFlag = 0; // to signal working threads to stop
volatile long oneThreadHasFinished = 0;

/*------------------ validateOptions ---------------------*/
bool validateOptions()
{
   if (globalData.url == NULL) {
      fprintf(stderr, "Must specify the URL to access with -s option\n");
      return false;
   }
   if (globalData.num_iter < 0xffffffff && globalData.remainingTestDurationSec < 0xffffffff) {
      fprintf(stderr, "One cannot specify both a time limit and a number of iterations\n");
      fprintf(stderr, "Only one exit condition must exist\n");
      return false;
   }
   if (globalData.num_threads > MAX_THREADS) {
      fprintf(stderr, "Maximum number of clients is %d\n", MAX_THREADS);
      return false;
   }
   if (globalData.num_threads < 1) {
      fprintf(stderr, "Must have at least one client. Selected number of clients is %d\n", globalData.num_threads);
      return false;
   }
   if (globalData.sec_periodical_stats < 1) {
      fprintf(stderr, "Period for intermediary statistics should be at least 1 sec\n");
      return false;
   }
   return true;
}


/*--------------------------- write_callback --------------------------------
* userp will point to a thread local buffer where we add statistics
*-----------------------------------------------------------------------------*/
static size_t write_callback(char *buffer, size_t size, size_t nitems, void *userp)
{
   unsigned sz = size*nitems;
   thread_data_t *tl = (thread_data_t*)userp;
   /* we just sink the data */
   //memcpy(userp, buffer, sz);
   tl->num_bytes += sz;
   return sz;
}


/*------------------------------- Workload -------------------------------
* Routine executed by each thread
* The parameter point to a thread_data_t structure
*-----------------------------------------------------------------------*/
void *workload(thread_data_t* tl)
{
   struct curl_slist *slist = NULL;
   char errorBuffer[CURL_ERROR_SIZE];

   CURL *curl = curl_easy_init();
   if (!curl) {
      fprintf(stderr, "Cannot initialize cURL\n"); exit(-1);
   }

   slist = curl_slist_append(slist, "Connection: Keep-Alive");
   slist = curl_slist_append(slist, "User-Agent: noagent/0.1");
   //slist = curl_slist_append(slist, "Pragma:");
   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);

   curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 32*1024);
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, tl);
   //curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
   //curl_easy_setopt(curl, CURLOPT_WRITEHEADER, hdr);
   curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
   curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30);
   curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30);
   // curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1); // needed for UNIX multithreaded programs
   //CURLOPT_COOKIE(3), CURLOPT_COOKIEJAR(3), CURLOPT_COOKIESESSION(3)
   //if (curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "MyCookieFile.txt") != CURLE_OK)
   //   fprintf(stderr, "Error with CURLOPT_COOKIEFILE\n");
   if (curl_easy_setopt(curl, CURLOPT_URL, globalData.url) != CURLE_OK) {
      fprintf(stderr, "Cannot set the URL. Exiting...\n");
      exit(-1);
   }

   for (unsigned i=0; i < globalData.num_iter; i++)
   {
      errorBuffer[0] = 0;
      const auto start = std::chrono::high_resolution_clock::now();
      CURLcode res = curl_easy_perform(curl);
      const auto end = std::chrono::high_resolution_clock::now();
      if (res != CURLE_OK) {
         size_t len = strlen(errorBuffer);
         if (len > 0) {
            fprintf(stderr, "Client %llu Error while getting page: %s\n", tl->client_id, errorBuffer);
         } else {
            fprintf(stderr, "Client %llu Error while getting page: %s\n", tl->client_id, curl_easy_strerror(res));
         }

         tl->num_errors++;
         if (tl->num_errors > globalData.max_errors_allowed_per_client)
         {
            fprintf(stderr, "Exiting due to too many errors\n");
            exit(-1);
         }
      }
      else {
         // verify correctness
         long code;
         curl_easy_getinfo(curl, CURLINFO_HTTP_CODE, &code);
         if (code != 200) {
            fprintf(stderr, "HTTP server replied with code %ld\n", code);
            tl->num_errors++;
            if (tl->num_errors > globalData.max_errors_allowed_per_client) {
               fprintf(stderr, "Exiting due to too many errors\n");
               exit(-1);
            }
         }
         else {
            tl->num_pages++;
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
            tl->cumm_resp_time += duration;
         }
      }
      if (finishFlag)
         break; // time to exit
      // apply the think time if any
      if (globalData.think_time > 0)
         std::this_thread::sleep_for(std::chrono::milliseconds(globalData.think_time));
   }
   //oneThreadHasFinished = 1; // signal the master that we are about to end
   /* always cleanup */
   curl_easy_cleanup(curl);

   return 0;
}


//---------------------------- HelpInfo --------------------------------------
void HelpInfo(const char *prgName)
{
   printf("Usage: %s [Options] -s url_to_access\n", prgName);
   printf("Options:\n");
   printf("-c ClientNum       Number of clients to simulate. Default is 1\n");
   printf("-d Delay           Think time between requests (ms). Default is 0\n");
   printf("-p PeriodicalStats Number of seconds for printing stats periodically. Default is 5 sec\n");
   printf("-r RepeatCount     How many times each client issues a request. Default is forever\n");
   printf("-t TestDuration    Expressed in seconds. Default is forever\n");

   exit(0);
}



//------------------------------------- main ---------------------------------
int main(int argc, char **argv)
{
   int i, c, n;
   unsigned long long cummRespTime = 0;
   unsigned long long totalData = 0;
   unsigned long long totalPages = 0;
   unsigned long long totalErrors = 0;
   unsigned long long lastTotalData=0, lastTotalPages=0, lastTotalErrors = 0;

   std::vector<std::thread> threadPool;
   threadPool.reserve(MAX_THREADS);

   // variabale initialization
   memset(tld, 0, sizeof(thread_data_t)*MAX_THREADS);
   memset(&globalData, 0, sizeof(globalData));
   globalData.num_iter = 0xffffffff; // default is forever
   globalData.num_threads = 1; // default value
   globalData.url = NULL;
   globalData.remainingTestDurationSec = 0xffffffff;
   globalData.sec_periodical_stats = 5;
   globalData.max_errors_allowed_per_client = 3;

   // read command line options and override the default values
   do {
      c = getopt(argc, argv, "c:d:hp:r:s:t:");
      if (c == '?' || c == ':') {
         fprintf(stderr, "Unknown option\n");
         HelpInfo(argv[0]);
         exit(-1);
      }
      switch (c) {
      case 'c': // Number of simulated clients
         globalData.num_threads = atoi(optarg);
         break;
      case 'd': // Delay between consecutive requests (think time)
         globalData.think_time = atoi(optarg);
         break;
      case 'h':
         HelpInfo(argv[0]);
         exit(0);
      case 'p': // Period of intermediary stats (in seconds)
         globalData.sec_periodical_stats = atoi(optarg);
         break;
      case 'r': // Repetition count (number of requests to be issued by each thread)
         globalData.num_iter = atoi(optarg);
         break;
      case 's': // Site URL to access
         n = strlen(optarg);
         globalData.url = new char[n+1];
         strcpy(globalData.url, optarg);
         break;
      case 't': // Duration of the entire test in seconds
         globalData.remainingTestDurationSec = atoi(optarg);
         break;
      }
   }while (c != -1);

   if (!validateOptions()) {
      HelpInfo(argv[0]);
      exit(-1);
   }


   printf("Will use %d clients and %u iterations\n", globalData.num_threads, globalData.num_iter);
   printf("Limit time = %d seconds\n", globalData.remainingTestDurationSec);
   printf("URL: %s\n", globalData.url);

   const std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
   // create the working threads
   try {
      for (i=0; i < globalData.num_threads; i++) {
         threadPool.push_back(std::thread(workload, &tld[i]));
      }
   }
   catch (const std::exception& e) {
      std::cerr << "EXCEPTION during thread creation: " << e.what() << std::endl;
      exit(-1);
   }


   if (globalData.num_iter == 0xffffffff)
   {
      while (true)
      {
         unsigned tNextEvent;

         if (globalData.sec_periodical_stats < globalData.remainingTestDurationSec)
            tNextEvent = globalData.sec_periodical_stats;
         else
            tNextEvent = globalData.remainingTestDurationSec;

         std::this_thread::sleep_for(std::chrono::milliseconds(tNextEvent*1000));
         globalData.remainingTestDurationSec -= tNextEvent;
         // either we need to end the program or to print stats
         if (globalData.remainingTestDurationSec == 0)
         {
            finishFlag = 1; // Tell the working threads to stop issuing requests
            break;
         }
         else // time to print some stats
         {
            totalData = totalPages = totalErrors = 0;
            for (i=0; i < globalData.num_threads; i++)
            {
               totalData   += tld[i].num_bytes;
               totalPages  += tld[i].num_pages;
               totalErrors += tld[i].num_errors;
            }
            printf("LastIntervalStats: Throughput: %.1f pages/sec  %.1f KB/sec   Errors:%llu\n",
               (double)(totalPages-lastTotalPages)/globalData.sec_periodical_stats,
               (double)(totalData-lastTotalData)/globalData.sec_periodical_stats/1024,
               (totalErrors-lastTotalErrors));
            // update last values
            lastTotalData   = totalData;
            lastTotalPages  = totalPages;
            lastTotalErrors = totalErrors;
         }
      }
   }
   else
      fprintf(stderr, "Stats will not be printed periodically\n");
   // must wait for threads to finish

   for(auto &t: threadPool)
      t.join();

   const std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();

   /* print statistics */

   totalData = totalPages = totalErrors = 0;
   cummRespTime = 0;
   for (i=0; i < globalData.num_threads; i++) {
      totalData += tld[i].num_bytes;
      totalPages += tld[i].num_pages;
      totalErrors += tld[i].num_errors;
      cummRespTime += tld[i].cumm_resp_time;
   }
   const std::chrono::duration<double, std::milli> elapsedMS = end - begin;
   printf("Number of pages = %llu\n", totalPages);
   printf("Data received = %llu KB (%f KB/s)\n", totalData/1024, totalData/1024.0/elapsedMS.count()*1000.0);
   printf("Number of errors = %llu\n", totalErrors);
   printf("Test took %.0f ms\n", elapsedMS.count());
   printf("Throughput = %.0f pages/sec\n", 1000.0*totalPages/elapsedMS.count());
   printf("Average response time = %llu usec\n", cummRespTime/totalPages);
   return 0;
}
