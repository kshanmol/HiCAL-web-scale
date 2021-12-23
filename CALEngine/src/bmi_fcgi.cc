#include <fstream>
#include <thread>
#include <fcgio.h>
#include "utils/simple-cmd-line-helper.h"
#include "bmi_disk.h"
#include "features.h"
#include "utils/feature_parser.h"
#include "utils/utils.h"
#include "json.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <vector>
#include <numeric>
#include <algorithm>
#include <iomanip>

using namespace std;
using json = nlohmann::json;

unordered_map<string, unique_ptr<BMI_disk>> SESSIONS;
unordered_map<string, string> SESSIONS_STR;
unique_ptr<Dataset> documents = nullptr;
unique_ptr<ParagraphDataset> paragraphs = nullptr;

string BASE_DIRR = "/zpipe/training/";

vector<string> split(string s, string delimiter){
	//std::string s = "scott>=tiger>=mushroom";
	//std::string delimiter = ">=";
	vector<string> tokens;
	if(s == "") return tokens;

	size_t pos = 0;
	string token;
	while ((pos = s.find(delimiter)) != string::npos) {
	    token = s.substr(0, pos);
	    tokens.push_back(token);
	    s.erase(0, pos + delimiter.length());
	}
	tokens.push_back(s);
	return tokens;
}

// Get the uri without following and preceding slashes
string parse_action_from_uri(string uri){
    int st = 0, end = int(uri.length())-1;

    while(st < uri.length() && uri[st] == '/') st++;
    while(end >= 0 && uri[end] == '/') end--;

    return uri.substr(st, end - st + 1);
}

// Use a proper library someday
// returns a vector of <key, value> in given url query string
vector<pair<string, string>> parse_query_string(string query_string){
    vector<pair<string, string>> key_vals;
    string key, val;
    bool valmode = false;
    for(char ch: query_string){
        if(ch == '&'){
            if(key.length() > 0)
                key_vals.push_back({key, val});
            key = val = "";
            valmode = false;
        }else if(ch == '=' && !valmode){
            valmode = true;
        }else{
            if(valmode)
                val.push_back(ch);
            else
                key.push_back(ch);
        }
    }
    if(key.length() > 0)
        key_vals.push_back({key, val});
    return key_vals;
}

// returns the content field of request
// Built upon http://chriswu.me/blog/writing-hello-world-in-fcgi-with-c-plus-plus/
string read_content(const FCGX_Request & request){
    fcgi_streambuf cin_fcgi_streambuf(request.in);
    istream request_stream(&cin_fcgi_streambuf);

    char * content_length_str = FCGX_GetParam("CONTENT_LENGTH", request.envp);
    unsigned long content_length = 0;

    if (content_length_str) {
        content_length = atoi(content_length_str);
    }

    char * content_buffer = new char[content_length];
    request_stream.read(content_buffer, content_length);
    content_length = request_stream.gcount();

    do request_stream.ignore(1024); while (request_stream.gcount() == 1024);
    string content(content_buffer, content_length);
    delete [] content_buffer;
    return content;
}

// Given info write to the request's response
void write_response(const FCGX_Request & request, int status, string content_type, string content){
    fcgi_streambuf cout_fcgi_streambuf(request.out);
    ostream response_stream(&cout_fcgi_streambuf);
    response_stream << "Status: " << to_string(status) << "\r\n"
                    << "Content-type: " << content_type << "\r\n"
                    << "\r\n"
                    << content << "\n";
    if(content.length() > 50)
         content = content.substr(0, 50) + "...";
    cerr<<"Wrote response: "<<content<<endl;
}


vector< pair<string, string> > get_negatives_bak(string idx_dir) {

        // current way of randomly selecting negatives
        // picks the first 10k files from idx_dir, and selects 100 at a random
        // will this bias the training in some way?
        // potentially better - pick first 1k from all 36 disks to form candidate set

        // offset, warc file pairs
        vector< pair<string, string> > negative_cands;
        vector< pair<string, string> > negatives;
        ifstream idx_file(idx_dir);
        string line;
        if (idx_file.is_open()){
                while (negative_cands.size() < 10000){
                        getline (idx_file, line);

                        string warc_dir, warc_file, warc_id, offset;
                        stringstream ssline(line);
                        ssline >> warc_dir >> warc_id >> offset;
                        warc_file = warc_dir.substr(0, warc_dir.size()-6);
                        warc_file += ".x";
                        negative_cands.push_back(make_pair(offset, warc_file));
                }
                idx_file.close();
        }
	
	cout << negative_cands.size() << endl;
        vector<unsigned int> indices(negative_cands.size());
        iota(indices.begin(), indices.end(), 0);
        //random_shuffle(indices.begin(), indices.end());
	
        int random_idx = 0;
	int lim = negative_cands.size() > 100 ? 100 : negative_cands.size();
        while (negatives.size() < lim) {
                negatives.push_back(negative_cands[indices[random_idx++]]);
        }
        return negatives;
}

int do_training_bak(string session_id, string seed_query) {

        string idx_dir =  "/zpipe/idx.all";
	string session_dir = BASE_DIRR + session_id + "/";
	string mkdir_cmd = "mkdir " + session_dir;
	system(mkdir_cmd.c_str());

        // First, we get a random 100 negatives
        // Offset, warc file pairs
        vector< pair<string, string> > negatives = get_negatives_bak(idx_dir);

        ofstream seeddoc;
        seeddoc.open(session_dir + "seeddoc.txt");
        
	istringstream ss(seed_query);
	string token;
	while(getline(ss, token, '%')){  // Write proper %20 tokenizer here
		seeddoc << token.substr(2) << " ";
	}
	seeddoc << endl;
        seeddoc.close();

        // Then we prepare the training data (from the .txt.1 files)
        ofstream negfiles, posfiles;
        negfiles.open(session_dir + "negfiles");
        for (auto neg : negatives) {
                string dest = neg.second + ".idx.dir/" + neg.first + ".txt.1";
                negfiles << dest << endl;
        }
        negfiles.close();
        posfiles.open(session_dir + "posfiles");
        posfiles << session_dir + "seeddoc.txt" << endl;
        posfiles.close();

        // Use tarall to calculate concordance for current training file
	char cmd[500];
	const char * cbase_dir = session_dir.c_str();
	sprintf(cmd, "tar cf - -T %snegfiles | /zpipe/tarall /dev/stdin 1> %snegatives.conc 2> %sneg.stderr", cbase_dir, cbase_dir, cbase_dir);
	system(cmd); cmd[0] = '\0';
	sprintf(cmd, "tar cf - -T %sposfiles | /zpipe/tarall /dev/stdin 1> %spositives.conc 2> %spos.stderr", cbase_dir, cbase_dir, cbase_dir);
	system(cmd); cmd[0] = '\0';

        // Use readdf to get svm.fil from concordance (uses the global df and N)
	sprintf(cmd, "/zpipe/readdf /zpipe/temp_df/df.all /zpipe/temp_df/dfN.all < %snegatives.conc > %snegatives.svm.fil", cbase_dir, cbase_dir);
	system(cmd); cmd[0] = '\0';
	
	sprintf(cmd, "/zpipe/readdf /zpipe/temp_df/df.all /zpipe/temp_df/dfN.all < %spositives.conc > %spositives.svm.fil", cbase_dir, cbase_dir);
	system(cmd); cmd[0] = '\0';

        // Label positives and negatives, concat into trainset
	sprintf(cmd, "cat %snegatives.svm.fil | awk '{printf -1; printf \" \"; for (i=2; i<NF; i++) printf $i \" \"; print $NF}' > %snegatives.trainset", cbase_dir, cbase_dir);
	system(cmd); cmd[0] = '\0';

	sprintf(cmd, "cat %spositives.svm.fil | awk '{printf 1; printf \" \"; for (i=2; i<NF; i++) printf $i \" \"; print $NF}' > %spositives.trainset", cbase_dir, cbase_dir);
	system(cmd); cmd[0] = '\0';

	sprintf(cmd, "cat %spositives.trainset %snegatives.trainset > %sall.trainset", cbase_dir, cbase_dir, cbase_dir);
	system(cmd); cmd[0] = '\0';
        
        // Then we train using sofia-ml
	sprintf(cmd, "/zpipe/sofia-ml-read-only/src/sofia-ml --learner_type logreg-pegasos --loop_type roc --lambda 0.1 --iterations 200000 --training_file %sall.trainset --dimensionality 9900000 --model_out %stest_model", cbase_dir, cbase_dir);
	system(cmd); cmd[0] = '\0';

	sprintf(cmd, "cat %snegatives.svm.fil %spositives.svm.fil | /zpipe/mywritex /dev/null > %sall.svm.bin", cbase_dir, cbase_dir, cbase_dir); 
	system(cmd); cmd[0] = '\0';

	// Convert the model into myreadT format and run myreadT

	sprintf(cmd, "cd %s; for x in test_model* ; do  (tr ' ' '\n' < $x | cat -n | sed -e '/       0$/d' > x$x &); done", cbase_dir);
	system(cmd); cmd[0] = '\0';
        sprintf(cmd, "/zpipe/myreadT %sxtest_model - /mnt/a536sing/sdaa1/cw-12-binx/*.binx > %sscores.txt", cbase_dir, cbase_dir);
	system(cmd); cmd[0] = '\0';
	
        string cmdc = "echo " + seed_query;
        system(cmdc.c_str());

        return 0;
}

// Handler for API endpoint /begin
void begin_session_view(const FCGX_Request & request, const vector<pair<string, string>> &params){
    string session_id, query, mode = "doc";
    vector<pair<string, int>> seed_judgments;
    int judgments_per_iteration = -1;
    bool async_mode = false;
    string seed_positives_str;

    for(auto kv: params){
        if(kv.first == "session_id"){
            session_id = kv.second;
        }else if(kv.first == "seed_query"){
            query = kv.second;
        }else if(kv.first == "mode"){
            // We ignore the mode field for now
        }else if(kv.first == "seed_positives"){
            seed_positives_str = kv.second;
	}else if(kv.first == "judgments_per_iteration"){
            try {
                judgments_per_iteration = stoi(kv.second);
            } catch (const invalid_argument& ia) {
                write_response(request, 400, "application/json", "{\"error\": \"Invalid judgments_per_iteration\"}");
                return;
            }
        }else if(kv.first == "async"){
            if(kv.second == "true" || kv.second == "True"){
                async_mode = true;
            }else if(kv.second == "false" || kv.second == "False"){
                async_mode = false;
            }
        }
    }

    if(SESSIONS.find(session_id) != SESSIONS.end()){
        write_response(request, 400, "application/json", "{\"error\": \"session already exists!!!\"}");
        return;
    }

    if(session_id.size() == 0 || query.size() == 0){
        write_response(request, 400, "application/json", "{\"error\": \"Non empty session_id and query required\"}");
        return;
    }

    vector<string> query_tokens = split(query, "%20");
    string seed_query = "";
    for(auto tok : query_tokens) {
	seed_query += tok + " ";
    }
    
    seed_positives_str.erase(remove(seed_positives_str.begin(), seed_positives_str.end(), ' '), seed_positives_str.end());
    vector<string> seed_positives = split(seed_positives_str, ",");
 
    SESSIONS_STR[session_id] = session_id;

    SESSIONS[session_id] = make_unique<BMI_disk>(
	session_id,
	seed_query,
        seed_positives,
	1,
	20, // batch size
	false, // async mode
	200000);

    //do_training_bak(session_id, query);

    // need proper json parsing!!
    write_response(request, 200, "application/json", "{\"session-id\": \""+session_id+"\"}");
}

// Handler for /delete_session
void delete_session_view(const FCGX_Request & request, const vector<pair<string, string>> &params){
    string session_id;

    for(auto kv: params){
        if(kv.first == "session_id"){
            session_id = kv.second;
        }
    }

    if(SESSIONS.find(session_id) == SESSIONS.end()){
        write_response(request, 404, "application/json", "{\"error\": \"session not found\"}");
        return;
    }

    SESSIONS.erase(session_id);

    write_response(request, 200, "application/json", "{\"session-id\": \"" + session_id + "\"}");
}

vector<string> get_docs_to_judge_bak(string session_id, int max_count) {

	vector<string> doc_ids;
	string session_dir = BASE_DIRR + session_id + "/";
	string scores_path = session_dir + "scores.txt";

        ifstream scores_file(scores_path);
        string line;
        if (scores_file.is_open()){
                while (doc_ids.size() < 1000){
                        getline (scores_file, line);
			if (line.find(session_id) == string::npos) {
				continue; // if the line isn't a score from the session model
			}
                        string m, model_path, rank, score, binx_path, offset;
                        stringstream ssline(line);

                        ssline >> m >> model_path >> rank >> score >> binx_path >> offset;

			string gvc12 = "gvc-12";
			string cw_binx = "cw-12-binx";
			string svm_binx = ".svm.binx";
			string idx_dir = ".idx.dir/";
			binx_path.replace(binx_path.find(cw_binx), cw_binx.length(), gvc12);
			binx_path.replace(binx_path.find(svm_binx), svm_binx.length(), idx_dir);
			string doc_id = binx_path + offset + ".txt.1"; 	
                        doc_ids.push_back(doc_id);
                }
                scores_file.close();
        }
	reverse(doc_ids.begin(), doc_ids.end());

	vector<string> top_doc_ids;
	int limit = max_count < doc_ids.size() ? max_count : doc_ids.size();
	for (int i = 0 ; i < limit ; i++) {
		top_doc_ids.push_back(doc_ids[i]);
	}
	return top_doc_ids;
}

// Fetch doc-ids in JSON
string get_docs(string session_id, bool batch_mode, int max_count, int num_top_terms = 10){
    
    //vector<string> doc_ids = get_docs_to_judge_bak(session_id, max_count);
    const unique_ptr<BMI_disk> &bmi = SESSIONS[session_id];
    if(batch_mode) {
        max_count = bmi->get_batch_remaining_count();
    }

    vector<string> doc_ids = bmi->get_doc_to_judge(max_count);
    cerr<<"Requested " << max_count << " docs for labeling, fetched " << doc_ids.size() << endl;
    string batch = to_string(bmi->get_current_iteration());

    string doc_json = "[";
    string top_terms_json = "{";
    for(string doc_id: doc_ids){
        if(doc_json.length() > 1)
            doc_json.push_back(',');
        if(top_terms_json.length() > 1)
            top_terms_json.push_back(',');
        doc_json += "\"" + doc_id + "\"";
    }
    doc_json.push_back(']');

    return "{\"session-id\": \"" + session_id + "\",\"batch\": \"" + batch + "\",\"docs\": " + doc_json + "}";
}

// Handler for /get_docs
void get_docs_view(const FCGX_Request & request, const vector<pair<string, string>> &params){
    string session_id;
    int max_count = 2;

    for(auto kv: params){
        if(kv.first == "session_id"){
            session_id = kv.second;
        }else if(kv.first == "max_count"){
            max_count = stoi(kv.second);
        }
    }

    if(session_id.size() == 0){
        write_response(request, 400, "application/json", "{\"error\": \"Non empty session_id required\"}");
        return;
    }

    if(SESSIONS.find(session_id) == SESSIONS.end()){
        write_response(request, 404, "application/json", "{\"error\": \"session not found\"}");
        return;
    }

    write_response(request, 200, "application/json", get_docs(session_id, true, max_count));
}

string escape_json(const string &s) {
    ostringstream o;
    for (auto c = s.cbegin(); c != s.cend(); c++) {
        if (*c == '"' || *c == '\\' || ('\x00' <= *c && *c <= '\x1f')) {
            o << "\\u"
              << hex << setw(4) << setfill('0') << (int)*c;
        } else {
            o << *c;
        }
    }
    return o.str();
}

string get_doc_content(string doc_path) {

	json j;
	//cerr << "Trying to open " << doc_path << endl;
	ifstream doc(doc_path);
	stringstream buffer;
	buffer << doc.rdbuf();
	//cerr << "Read characters: " << buffer.str().size() << endl;
	j["doc_content"] = buffer.str();
	//return j.dump();
        //return "{\"doc_content\": \"" + buffer.str() + "\"}";
	return buffer.str();
}

string get_doc_path(string doc_id) {
    string doc_tokens = "";
    vector<string> tokens = split(doc_id, "%2F");
    for(auto tok : tokens) {
         doc_tokens += tok + "/";
    }
    string doc_path = doc_tokens.substr(0, doc_tokens.size()-1);
    cerr << doc_path << " YO" << endl;
    return doc_path;
}

void get_content_view(const FCGX_Request & request, const vector<pair<string, string>> &params){
	string disk, folder, offset;
	string doc_id;
	for(auto kv: params){
		if(kv.first == "doc_id"){
			doc_id = kv.second;
		}
	}
	string doc_path = get_doc_path(doc_id); 

	if(doc_path.size() == 0){
		write_response(request, 400, "application/json", "{\"error\": \"Non empty doc_id required\"}");
	}
	write_response(request, 200, "application/json", get_doc_content(doc_path));

}

// Handler for /judge
void judge_view(const FCGX_Request & request, const vector<pair<string, string>> &params){
    string session_id, doc_id;
    int rel = -2;

    for(auto kv: params){
        if(kv.first == "session_id"){
            session_id = kv.second;
        }else if(kv.first == "doc_id"){
            doc_id = kv.second;
        }else if(kv.first == "rel"){
            rel = stoi(kv.second);
        }
    }

    string doc_path = get_doc_path(doc_id); 

    if(session_id.size() == 0 || doc_path.size() == 0){
        write_response(request, 400, "application/json", "{\"error\": \"Non empty session_id and doc_id required\"}");
    }

    if(SESSIONS.find(session_id) == SESSIONS.end()){
        write_response(request, 404, "application/json", "{\"error\": \"session not found\"}");
        return;
    }

    const unique_ptr<BMI_disk> &bmi = SESSIONS[session_id];

    // Not handling invalid doc ids for now
    //if(bmi->get_dataset()->get_index(doc_id) == bmi->get_dataset()->NPOS){
    //    write_response(request, 404, "application/json", "{\"error\": \"doc_id not found\"}");
    //    return;
    //}

    if(rel < -1 || rel > 1){
        write_response(request, 400, "application/json", "{\"error\": \"rel can either be -1, 0 or 1\"}");
        return;
    }

    bmi->record_judgment(doc_path, rel);
    write_response(request, 200, "application/json", get_docs(session_id, true, 20));
}

void log_request(const FCGX_Request & request, const vector<pair<string, string>> &params){
    cerr<<string(FCGX_GetParam("RELATIVE_URI", request.envp))<<endl;
    cerr<<FCGX_GetParam("REQUEST_METHOD", request.envp)<<endl;
    for(auto kv: params){
        cerr<<kv.first<<" "<<kv.second<<endl;
    }
    cerr<<endl;
}

void process_request(const FCGX_Request & request) {
    string action = parse_action_from_uri(FCGX_GetParam("RELATIVE_URI", request.envp));
    string method = FCGX_GetParam("REQUEST_METHOD", request.envp);

    vector<pair<string, string>> params;

    if(method == "GET")
        params = parse_query_string(FCGX_GetParam("QUERY_STRING", request.envp));
    else if(method == "POST" || method == "DELETE")
        params = parse_query_string(read_content(request));

    log_request(request, params);

    if(action == "begin"){
        if(method == "POST"){
            begin_session_view(request, params);
        }
    }else if(action == "get_docs"){
        if(method == "GET"){
            get_docs_view(request, params);
        }
    } else if(action == "get_content"){
        if(method == "GET"){
            get_content_view(request, params);
        }
    }else if(action == "judge"){
        if(method == "POST"){
            judge_view(request, params);
        }
    /*}else if(action == "get_ranklist"){
        if(method == "GET"){
            get_ranklist(request, params);
        }*/
    }else if(action == "delete_session"){
        if(method == "DELETE"){
            delete_session_view(request, params);
        }
    /*}else if(action == "log"){
        if(method == "GET"){
            log_view(request, params);
        }*/
    }
}

void fcgi_listener(){
    FCGX_Request request;
    FCGX_InitRequest(&request, 0, 0);

    while (FCGX_Accept_r(&request) == 0) {
        process_request(request);
    }
}

int main(int argc, char **argv){
    AddFlag("--doc-features", "Path of the file with list of document features", string(""));
    AddFlag("--para-features", "Path of the file with list of paragraph features", string(""));
    AddFlag("--df", "Path of the file with list of terms and their document frequencies", string(""));
    AddFlag("--threads", "Number of threads to use for scoring", int(8));
    AddFlag("--help", "Show Help", bool(false));

    ParseFlags(argc, argv);

    if(CMD_LINE_BOOLS["--help"]){
        ShowHelp();
        return 0;
    }

    if(CMD_LINE_STRINGS["--doc-features"].length() == 0){
        cerr<<"Required argument --doc-features missing"<<endl;
        return -1;
    }

    FCGX_Init();

    vector<thread> fastcgi_threads;
    for(int i = 0;i < 50; i++){
        fastcgi_threads.push_back(thread(fcgi_listener));
    }

    for(auto &t: fastcgi_threads){
        t.join();
    }

    return 0;
}
