#include "simdjson.h"

#ifndef _MSC_VER
#include "linux-perf-events.h"
#include <unistd.h>
#ifdef __linux__
#include <libgen.h>
#endif //__linux__
#endif // _MSC_VER

#include <memory>

#include "benchmark.h"

SIMDJSON_PUSH_DISABLE_ALL_WARNINGS

// #define RAPIDJSON_SSE2 // bad for performance
// #define RAPIDJSON_SSE42 // bad for performance
#include "rapidjson/document.h"
#include "rapidjson/reader.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include "sajson.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#ifdef ALLPARSER

#include "fastjson.cpp"
#include "fastjson_dom.cpp"
#include "gason.cpp"

#include "json11.cpp"
extern "C" {
#include "cJSON.c"
#include "cJSON.h"
#include "jsmn.c"
#include "jsmn.h"
#include "ujdecode.h"
#include "ultrajsondec.c"
}

#include "jsoncpp.cpp"
#include "json/json.h"

#endif

SIMDJSON_POP_DISABLE_WARNINGS

using namespace rapidjson;

#ifdef ALLPARSER
// fastjson has a tricky interface
void on_json_error(void *, UNUSED const fastjson::ErrorContext &ec) {
  // std::cerr<<"ERROR: "<<ec.mesg<<std::endl;
}
bool fastjson_parse(const char *input) {
  fastjson::Token token;
  fastjson::dom::Chunk chunk;
  return fastjson::dom::parse_string(input, &token, &chunk, 0, &on_json_error,
                                     NULL);
}
// end of fastjson stuff
#endif

never_inline size_t sum_line_lengths(char * data, size_t length) {
  std::stringstream is;
  is.rdbuf()->pubsetbuf(data, length);
  std::string line;
  size_t sumofalllinelengths{0};
  while(getline(is, line)) {
    sumofalllinelengths += line.size();
  }
  return sumofalllinelengths;
}


bool bench(const char *filename, bool verbose, bool just_data, int repeat_multiplier) {
  auto [p, err] = simdjson::padded_string::load(filename);
  if (err) {
    std::cerr << "Could not load the file " << filename << std::endl;
    return false;
  }

  int repeat = (50000000 * repeat_multiplier) / p.size();
  if (repeat < 10) { repeat = 10; }

  if (verbose) {
    std::cout << "Input " << filename << " has ";
    if (p.size() > 1024 * 1024)
      std::cout << p.size() / (1024 * 1024) << " MB";
    else if (p.size() > 1024)
      std::cout << p.size() / 1024 << " KB";
    else
      std::cout << p.size() << " B";
    std::cout << ": will run " << repeat << " iterations." << std::endl;
  }
  int volume = p.size();
  if (just_data) {
    printf("%-42s %20s %20s %20s %20s \n", "name", "cycles_per_byte",
           "cycles_per_byte_err", "gb_per_s", "gb_per_s_err");
  }
  if (!just_data) {
    size_t lc = sum_line_lengths(p.data(), p.size());
    BEST_TIME("getline ",sum_line_lengths(p.data(), p.size()) , lc, , 
       repeat, volume, !just_data);
  }

  if (!just_data)
    BEST_TIME("simdjson (dynamic mem) ", simdjson::dom::parser().parse(p).error(), simdjson::SUCCESS,
              , repeat, volume, !just_data);
  // (static alloc)
  simdjson::dom::parser parser;
  BEST_TIME("simdjson ", parser.parse(p).error(), simdjson::SUCCESS, , repeat, volume,
            !just_data);
 
  rapidjson::Document d;

  char *buffer = (char *)malloc(p.size() + 1);
  memcpy(buffer, p.data(), p.size());
  buffer[p.size()] = '\0';
#ifndef ALLPARSER
  if (!just_data)
#endif
  {
    memcpy(buffer, p.data(), p.size());
    BEST_TIME("RapidJSON  ",
              d.Parse<kParseValidateEncodingFlag>((const char *)buffer)
                  .HasParseError(),
              false, , repeat, volume,
              !just_data);
  }
#ifndef ALLPARSER
  if (!just_data)
#endif
  {
    memcpy(buffer, p.data(), p.size());
    BEST_TIME("RapidJSON (accurate number parsing)  ",
              d.Parse<kParseValidateEncodingFlag|kParseFullPrecisionFlag>((const char *)buffer)
                  .HasParseError(),
              false, , repeat, volume,
              !just_data);
  }
  BEST_TIME("RapidJSON (insitu)",
            d.ParseInsitu<kParseValidateEncodingFlag>(buffer).HasParseError(),
            false,
            memcpy(buffer, p.data(), p.size()) && (buffer[p.size()] = '\0'),
            repeat, volume, !just_data);
  BEST_TIME("RapidJSON (insitu, accurate number parsing)",
            d.ParseInsitu<kParseValidateEncodingFlag|kParseFullPrecisionFlag>(buffer).HasParseError(),
            false,
            memcpy(buffer, p.data(), p.size()) && (buffer[p.size()] = '\0'),
            repeat, volume, !just_data);
#ifndef ALLPARSER
  if (!just_data)
#endif
    BEST_TIME("sajson (dynamic mem)",
              sajson::parse(sajson::dynamic_allocation(),
                            sajson::mutable_string_view(p.size(), buffer))
                  .is_valid(),
              true, memcpy(buffer, p.data(), p.size()), repeat, volume,
              !just_data);

  size_t ast_buffer_size = p.size();
  size_t *ast_buffer = (size_t *)malloc(ast_buffer_size * sizeof(size_t));
  //  (static alloc, insitu)
  BEST_TIME(
      "sajson",
      sajson::parse(sajson::bounded_allocation(ast_buffer, ast_buffer_size),
                    sajson::mutable_string_view(p.size(), buffer))
          .is_valid(),
      true, memcpy(buffer, p.data(), p.size()), repeat, volume, !just_data);

  memcpy(buffer, p.data(), p.size());
  size_t expected = json::parse(p.data(), p.data() + p.size()).size();
  BEST_TIME("nlohmann-json", json::parse(buffer, buffer + p.size()).size(),
            expected, , repeat, volume,
            !just_data);

#ifdef ALLPARSER
  std::string json11err;
  BEST_TIME("dropbox (json11)     ",
            ((json11::Json::parse(buffer, json11err).is_null()) ||
             (!json11err.empty())),
            false, memcpy(buffer, p.data(), p.size()), repeat, volume,
            !just_data);

  BEST_TIME("fastjson             ", fastjson_parse(buffer), true,
            memcpy(buffer, p.data(), p.size()), repeat, volume, !just_data);
  JsonValue value;
  JsonAllocator allocator;
  char *endptr;
  BEST_TIME("gason             ", jsonParse(buffer, &endptr, &value, allocator),
            JSON_OK, memcpy(buffer, p.data(), p.size()), repeat, volume,
            !just_data);
  void *state;
  BEST_TIME("ultrajson         ",
            (UJDecode(buffer, p.size(), NULL, &state) == NULL), false,
            memcpy(buffer, p.data(), p.size()), repeat, volume, !just_data);

  {
    std::unique_ptr<jsmntok_t[]> tokens =
        std::make_unique<jsmntok_t[]>(p.size());
    jsmn_parser jparser;
    jsmn_init(&jparser);
    memcpy(buffer, p.data(), p.size());
    buffer[p.size()] = '\0';
    BEST_TIME(
        "jsmn           ",
        (jsmn_parse(&jparser, buffer, p.size(), tokens.get(), p.size()) > 0),
        true, jsmn_init(&jparser), repeat, volume, !just_data);
  }
  memcpy(buffer, p.data(), p.size());
  buffer[p.size()] = '\0';
  cJSON *tree = cJSON_Parse(buffer);
  BEST_TIME("cJSON           ", ((tree = cJSON_Parse(buffer)) != NULL), true,
            cJSON_Delete(tree), repeat, volume, !just_data);
  cJSON_Delete(tree);

  Json::CharReaderBuilder b;
  Json::CharReader *json_cpp_reader = b.newCharReader();
  Json::Value root;
  Json::String errs;
  BEST_TIME("jsoncpp           ",
            json_cpp_reader->parse(buffer, buffer + volume, &root, &errs), true,
            , repeat, volume, !just_data);
  delete json_cpp_reader;
#endif
  if (!just_data)
    BEST_TIME("memcpy            ",
              (memcpy(buffer, p.data(), p.size()) == buffer), true, , repeat,
              volume, !just_data);
#ifdef __linux__
  if (!just_data) {
    printf("\n \n <doing additional analysis with performance counters (Linux "
           "only)>\n");
    std::vector<int> evts;
    evts.push_back(PERF_COUNT_HW_CPU_CYCLES);
    evts.push_back(PERF_COUNT_HW_INSTRUCTIONS);
    evts.push_back(PERF_COUNT_HW_BRANCH_MISSES);
    evts.push_back(PERF_COUNT_HW_CACHE_REFERENCES);
    evts.push_back(PERF_COUNT_HW_CACHE_MISSES);
    LinuxEvents<PERF_TYPE_HARDWARE> unified(evts);
    std::vector<unsigned long long> results;
    std::vector<unsigned long long> stats;
    results.resize(evts.size());
    stats.resize(evts.size());
    std::fill(stats.begin(), stats.end(), 0); // unnecessary
    for (int i = 0; i < repeat; i++) {
      unified.start();
      auto [doc, parse_error] = parser.parse(p);
      if (parse_error)
        printf("bug\n");
      unified.end(results);
      std::transform(stats.begin(), stats.end(), results.begin(), stats.begin(),
                     std::plus<unsigned long long>());
    }
    printf("simdjson : cycles %10.0f instructions %10.0f branchmisses %10.0f "
           "cacheref %10.0f cachemisses %10.0f  bytespercachemiss %10.0f "
           "inspercycle %10.1f insperbyte %10.1f\n",
           stats[0] * 1.0 / repeat, stats[1] * 1.0 / repeat,
           stats[2] * 1.0 / repeat, stats[3] * 1.0 / repeat,
           stats[4] * 1.0 / repeat, volume * repeat * 1.0 / stats[2],
           stats[1] * 1.0 / stats[0], stats[1] * 1.0 / (volume * repeat));

    std::fill(stats.begin(), stats.end(), 0);
    for (int i = 0; i < repeat; i++) {
      memcpy(buffer, p.data(), p.size());
      buffer[p.size()] = '\0';
      unified.start();
      if (d.ParseInsitu<kParseValidateEncodingFlag>(buffer).HasParseError() !=
          false)
        printf("bug\n");
      unified.end(results);
      std::transform(stats.begin(), stats.end(), results.begin(), stats.begin(),
                     std::plus<unsigned long long>());
    }
    printf("RapidJSON: cycles %10.0f instructions %10.0f branchmisses %10.0f "
           "cacheref %10.0f cachemisses %10.0f  bytespercachemiss %10.0f "
           "inspercycle %10.1f insperbyte %10.1f\n",
           stats[0] * 1.0 / repeat, stats[1] * 1.0 / repeat,
           stats[2] * 1.0 / repeat, stats[3] * 1.0 / repeat,
           stats[4] * 1.0 / repeat, volume * repeat * 1.0 / stats[2],
           stats[1] * 1.0 / stats[0], stats[1] * 1.0 / (volume * repeat));

    std::fill(stats.begin(), stats.end(), 0); // unnecessary
    for (int i = 0; i < repeat; i++) {
      memcpy(buffer, p.data(), p.size());
      unified.start();
      if (sajson::parse(sajson::bounded_allocation(ast_buffer, ast_buffer_size),
                        sajson::mutable_string_view(p.size(), buffer))
              .is_valid() != true)
        printf("bug\n");
      unified.end(results);
      std::transform(stats.begin(), stats.end(), results.begin(), stats.begin(),
                     std::plus<unsigned long long>());
    }
    printf("sajson   : cycles %10.0f instructions %10.0f branchmisses %10.0f "
           "cacheref %10.0f cachemisses %10.0f  bytespercachemiss %10.0f "
           "inspercycle %10.1f insperbyte %10.1f\n",
           stats[0] * 1.0 / repeat, stats[1] * 1.0 / repeat,
           stats[2] * 1.0 / repeat, stats[3] * 1.0 / repeat,
           stats[4] * 1.0 / repeat, volume * repeat * 1.0 / stats[2],
           stats[1] * 1.0 / stats[0], stats[1] * 1.0 / (volume * repeat));

  }
#endif //  __linux__

  free(ast_buffer);
  free(buffer);
  return true;
}

int main(int argc, char *argv[]) {
  bool verbose = false;
  bool just_data = false;
  double repeat_multiplier = 1;
  int c;
  while ((c = getopt(argc, argv, "r:vt")) != -1)
    switch (c) {
    case 'r':
      repeat_multiplier = atof(optarg);
      break;
    case 't':
      just_data = true;
      break;
    case 'v':
      verbose = true;
      break;
    default:
      abort();
    }
  if (optind >= argc) {
    std::cerr << "Usage: " << argv[0] << " <jsonfile>" << std::endl;
    std::cerr << "Or " << argv[0] << " -v <jsonfile>" << std::endl;
    std::cerr << "The '-t' flag outputs a table." << std::endl;
    std::cerr << "The '-r <N>' flag sets the repeat multiplier: set it above 1 to do more iterations, and below 1 to do fewer." << std::endl;
    exit(1);
  }
  int result = EXIT_SUCCESS;
  for (int fileind = optind; fileind < argc; fileind++) {
    if (!bench(argv[fileind], verbose, just_data, repeat_multiplier)) { result = EXIT_FAILURE; }
    printf("\n\n");
  }
  return result;
}
