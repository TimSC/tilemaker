#include "Simple-Web-Server/server_http.hpp"
#include "external/sqlite_modern_cpp.h"
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

using namespace std;
namespace po = boost::program_options;
using HttpServer = SimpleWeb::Server<SimpleWeb::HTTP>;

#include <iostream>
#include <fstream>
#include <chrono>
#include <thread>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

// Rendering tiles on request (--render): each is made by running tilemaker
// for that tile, reading the map database, into a directory of its own
struct RenderSettings {
    string tilemaker;
    string config;
    string process;
    string dbConnection;
    string tablePrefix;
    int timeoutSeconds;
};

// Runs a program, its output discarded, and waits for it. Returns false, with
// the reason, if it couldn't be run, failed, or ran out of time (it is then killed).
static bool runProgram(const vector<string> &args, int timeoutSeconds, string &error) {
    vector<char*> argv;
    for (const string &arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t pid;
    int rc = posix_spawn(&pid, argv[0], &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) { error = string("couldn't run ") + args[0] + ": " + strerror(rc); return false; }

    auto deadline = chrono::steady_clock::now() + chrono::seconds(timeoutSeconds);
    int status = 0;
    while (true) {
        pid_t done = waitpid(pid, &status, WNOHANG);
        if (done == pid) break;
        if (done < 0) { error = strerror(errno); return false; }
        if (chrono::steady_clock::now() > deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            error = "timed out after " + to_string(timeoutSeconds) + "s";
            return false;
        }
        this_thread::sleep_for(chrono::milliseconds(10));
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        error = "tilemaker failed (" + (WIFEXITED(status) ? "exit status " + to_string(WEXITSTATUS(status)) : string("killed by a signal")) + ")";
        return false;
    }
    return true;
}

// Renders a tile. Returns false on failure; an empty tile (nothing there) is
// returned as an empty string.
static bool renderTile(const RenderSettings &settings, unsigned zoom, unsigned x, unsigned y, string &tile, string &error) {
    char dirTemplate[] = "/tmp/tilemaker-tile-XXXXXX";
    if (mkdtemp(dirTemplate) == nullptr) { error = strerror(errno); return false; }
    const boost::filesystem::path dir(dirTemplate);

    vector<string> args = {settings.tilemaker,
        settings.dbConnection.empty() ? string("--db") : "--db=" + settings.dbConnection,
        "--table-prefix", settings.tablePrefix,
        "--tile", to_string(zoom) + "/" + to_string(x) + "/" + to_string(y),
        "--output", (dir / "tiles").string(),
        "--config", settings.config, "--process", settings.process,
        "--threads", "1", "--quiet"};
    bool ok = runProgram(args, settings.timeoutSeconds, error);
    if (ok) {
        // tilemaker writes nothing for a tile with nothing in it
        ifstream in((dir / "tiles" / to_string(zoom) / to_string(x) / (to_string(y) + ".pbf")).string(), ios::binary);
        tile.assign(istreambuf_iterator<char>(in), istreambuf_iterator<char>());
    }
    boost::system::error_code ec;
    boost::filesystem::remove_all(dir, ec);
    return ok;
}

// Sends a tile, saying if it is gzipped (tilemaker's "compress" setting)
static void writeTile(shared_ptr<SimpleWeb::Server<SimpleWeb::HTTP>::Response> response, const string &tile) {
    SimpleWeb::CaseInsensitiveMultimap header;
    header.emplace("Access-Control-Allow-Origin", "*");
    if (tile.empty()) {
        response->write(SimpleWeb::StatusCode::success_no_content, header);
        return;
    }
    if (tile.size() >= 2 && (unsigned char)tile[0] == 0x1f && (unsigned char)tile[1] == 0x8b)
        header.emplace("Content-Encoding", "gzip");
    response->write(tile, header);
}

inline unsigned char from_hex (unsigned char ch) {
    if (ch <= '9' && ch >= '0')
        ch -= '0';
    else if (ch <= 'f' && ch >= 'a')
        ch -= 'a' - 10;
    else if (ch <= 'F' && ch >= 'A')
        ch -= 'A' - 10;
    else
        ch = 0;
    return ch;
}

const std::string urldecode (const std::string& str) {
    string result;
    string::size_type i;
    for (i = 0; i < str.size(); ++i)
    {
        if (str[i] == '+')
        {
            result += ' ';
        }
        else if (str[i] == '%' && str.size() > i+2)
        {
            const unsigned char ch1 = from_hex(str[i+1]);
            const unsigned char ch2 = from_hex(str[i+2]);
            const unsigned char ch = (ch1 << 4) | ch2;
            result += ch;
            i += 2;
        }
        else
        {
            result += str[i];
        }
    }
    return result;
}

int main(int argc, char* argv[]) {
    string input;
    string staticPath;
    unsigned port;
    bool render = false;
    string bbox;
    unsigned threads;
    RenderSettings settings;

    // tilemaker is expected beside this program
    boost::system::error_code ec;
    boost::filesystem::path self = boost::filesystem::read_symlink("/proc/self/exe", ec);
    string defaultTilemaker = ec ? "tilemaker" : (self.parent_path() / "tilemaker").string();

    po::options_description desc("tilemaker-server\nServe tiles from an .mbtiles archive, or render them from the map database as they are asked for\n\nAvailable options");
    desc.add_options()
        ("help","show help message")
        ("input",po::value< string >(&input),"source .mbtiles")
        ("render", po::bool_switch(&render), "render each tile from the map database with tilemaker, rather than serve an .mbtiles")
        ("static", po::value< string >(&staticPath)->default_value("static"), "path of static files")
        ("port",po::value< unsigned >(&port)->default_value(8081), "port to serve tiles");
    po::options_description renderOptions("Rendering options (with --render)");
    renderOptions.add_options()
        ("bbox", po::value< string >(&bbox), "area the map shows when it opens, as minlon,minlat,maxlon,maxlat")
        ("db", po::value< string >(&settings.dbConnection)->default_value(""), "libpq connection string; by default the PG* environment variables are used")
        ("table-prefix", po::value< string >(&settings.tablePrefix)->default_value("pycrocosm_mod_"), "map database table set to read")
        ("config", po::value< string >(&settings.config)->default_value("config.json"), "tilemaker config JSON file")
        ("process", po::value< string >(&settings.process)->default_value("process.lua"), "tilemaker tag-processing Lua file")
        ("tilemaker", po::value< string >(&settings.tilemaker)->default_value(defaultTilemaker), "tilemaker program")
        ("threads", po::value< unsigned >(&threads)->default_value(4), "tiles rendered at once")
        ("timeout", po::value< int >(&settings.timeoutSeconds)->default_value(60), "seconds a tile may take");
    desc.add(renderOptions);
    po::positional_options_description p;
    p.add("input", -1);
    po::variables_map vm;
    try {
        po::store(po::command_line_parser(argc, argv).options(desc).positional(p).run(), vm);
    } catch (const po::unknown_option& ex) {
        cerr << "Unknown option: " << ex.get_option_name() << endl;
        return -1;
    }
    po::notify(vm);
    if (vm.count("help")) { std::cout << desc << std::endl; return 1; }
    if (render == (vm.count("input") > 0)) { std::cerr << "Give an .mbtiles file, or --render. Run with --help to find out more." << std::endl; return -1; }
    if (render && bbox.empty()) { std::cerr << "--render needs a --bbox for the map to open on." << std::endl; return -1; }

    HttpServer server;
    server.config.port = port;
    server.config.thread_pool_size = render ? max(threads, 1u) : 1;
    // A tile can take as long as tilemaker is allowed
    server.config.timeout_request = max<long>(5, settings.timeoutSeconds + 5);

    if (render) {
        // tilemaker renders from z11 (see docs/RUNNING.md); lower tiles are empty
        server.resource["^/([0-9]+)/([0-9]+)/([0-9]+).pbf$"]["GET"] = [&settings](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
            unsigned zoom = stoul(request->path_match[1]);
            unsigned x = stoul(request->path_match[2]);
            unsigned y = stoul(request->path_match[3]);
            if (zoom < 11 || zoom > 22 || x >= (1u << zoom) || y >= (1u << zoom)) { writeTile(response, ""); return; }

            auto start = chrono::steady_clock::now();
            string tile, error;
            if (!renderTile(settings, zoom, x, y, tile, error)) {
                cerr << zoom << "/" << x << "/" << y << ": " << error << endl;
                response->write(SimpleWeb::StatusCode::server_error_internal_server_error, "Couldn't render the tile: " + error);
                return;
            }
            auto ms = chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - start).count();
            cout << zoom << "/" << x << "/" << y << ": " << tile.size() << " bytes in " << ms << "ms" << endl;
            writeTile(response, tile);
        };

        server.resource["^/metadata$"]["GET"] = [&bbox](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
            response->write("{\"name\":\"tilemaker-core\",\"format\":\"pbf\",\"minzoom\":\"11\",\"bounds\":\"" + bbox + "\"}");
        };
    }

    sqlite::database db(render ? ":memory:" : input);

    if (!render) {
    server.resource["^/([0-9]+)/([0-9]+)/([0-9]+).pbf$"]["GET"] = [&db](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
        int32_t zoom = stoi(request->path_match[1]);
        int32_t col = stoi(request->path_match[2]);
        int32_t y = stoi(request->path_match[3]);
        int tmsY = pow(2,zoom) - 1 - y;
        string tile;
        db << "SELECT tile_data FROM tiles WHERE zoom_level=? AND tile_column=? AND tile_row=?" << zoom << col << tmsY >> [&](vector<char> pbfBlob) {
            tile.assign(pbfBlob.begin(), pbfBlob.end());
        };
        writeTile(response, tile);
    };

    server.resource["^/metadata$"]["GET"] = [&db](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
       rapidjson::Document document;
       document.SetObject();
       rapidjson::Document::AllocatorType& allocator = document.GetAllocator();
        db << "SELECT name, value FROM metadata;" >> [&](string name, string value) {
            if (name == "json") {
                rapidjson::Document subDocument;
                subDocument.Parse(value.c_str());
                document.AddMember("json",subDocument,allocator);

            } else {
                rapidjson::Value nameVal;
                nameVal.SetString(name.c_str(), allocator);
                rapidjson::Value valueVal;
                valueVal.SetString(value.c_str(), allocator);
                document.AddMember(nameVal, valueVal, allocator);
            }
        };
       rapidjson::StringBuffer stringbuf;
       rapidjson::Writer<rapidjson::StringBuffer> writer(stringbuf);
       document.Accept(writer);
       response->write(stringbuf.GetString());
    };
    }

    server.default_resource["GET"] = [&staticPath](shared_ptr<HttpServer::Response> response, shared_ptr<HttpServer::Request> request) {
        try {
            auto pathstr = urldecode(request->path);
            if (pathstr == "/") pathstr = "/index.html";
            auto web_root_path = boost::filesystem::canonical(staticPath);
            auto path = boost::filesystem::canonical(web_root_path / pathstr);
            if(distance(web_root_path.begin(), web_root_path.end()) > distance(path.begin(), path.end()) ||
                !equal(web_root_path.begin(), web_root_path.end(), path.begin()))
                throw invalid_argument("path must be within root path");

            SimpleWeb::CaseInsensitiveMultimap header;
            auto ifs = make_shared<ifstream>();
            ifs->open(path.string(), ifstream::in | ios::binary | ios::ate);
            if(*ifs) {
                auto length = ifs->tellg();
                ifs->seekg(0, ios::beg);
                header.emplace("Content-Length", to_string(length));
                response->write(header);
                vector<char> buffer(length);
                ifs->read(&buffer[0],length);
                response->write(&buffer[0],length);
            } else {
                throw invalid_argument("could not read file");
            }
        } catch(const exception &e) {
            response->write(SimpleWeb::StatusCode::client_error_bad_request, "Could not open path " + request->path + ": " + e.what());
        }
    };

    cout << "Starting local server on port " << server.config.port << (render ? ", rendering tiles with " + settings.tilemaker : "") << endl;
    server.start();
}