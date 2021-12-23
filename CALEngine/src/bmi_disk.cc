#include <iostream>
#include <string>
#include <mutex>
#include <thread>
#include <algorithm>
#include <climits>
#include "bmi_disk.h"
#include "classifier.h"
#include "utils/utils.h"

#include <cstdlib>
#include <cstring>
#include <cmath>
#include <sstream>
#include <vector>
#include <numeric>
#include <fstream>

using namespace std;
BMI_disk::BMI_disk(string _session_id,
         string _seed_query,
         vector<string> _seed_positives,
         int _num_threads,
         int _judgments_per_iteration,
         bool _async_mode,
         int _training_iterations,
         bool initialize)
    :num_threads(_num_threads),
    judgments_per_iteration(_judgments_per_iteration), // batch size in sync mode
    async_mode(_async_mode),
    seed_query(_seed_query),
    seed_positives(_seed_positives),
    session_id(_session_id),
    training_iterations(_training_iterations)
{
    is_bmi = (judgments_per_iteration == -1);
    if(is_bmi || _async_mode)
        judgments_per_iteration = 1;

    session_dir = base_dir + session_id + "/";
    current_round = 1;

    // Make the directory
    string mkdir_cmd = "mkdir " + session_dir;
    system(mkdir_cmd.c_str());
    string touch_seedpos_cmd = "touch " + session_dir + "seed.posfiles";
    system(touch_seedpos_cmd.c_str());
    string touch_total_cmd = "touch " + session_dir + "total.N";
    system(touch_total_cmd.c_str());
    
    init_training_data();
    init_misinfo_qrels();

    if(initialize)
        perform_iteration();
}

void BMI_disk::perform_iteration(){
    lock_guard<mutex> lock(state_mutex);
    auto results = perform_training_iteration();
    cerr<<"Fetched "<<results.size()<<" documents"<<endl;
    add_to_judgment_list(results);
    if(!async_mode){
        state.next_iteration_target = state.next_iteration_target + judgments_per_iteration;
        //if(is_bmi)
        //    judgments_per_iteration += (judgments_per_iteration + 9)/10;
    }
    state.cur_iteration++;
    cerr<< "Training state: Iteration " << state.cur_iteration << ", Next target " << state.next_iteration_target << endl;
}

void BMI_disk::perform_iteration_async(){
    if(async_training_mutex.try_lock()){
        while(!training_cache.empty()){
            perform_iteration();
        }
        async_training_mutex.unlock();
    }
}

void extract_txt_files(string warc_file, string offset) {
	// Just extracts the warc file for now, will also do lynx dump for better display later

	char cmd[500];
        const char * c_warc_file = warc_file.c_str();
	const char * c_offset = offset.c_str();
        sprintf(cmd, "/zpipe/zchunk -s %s < %s.x > %s.idx.dir/%s", c_offset, c_warc_file, c_warc_file, c_offset);
        system(cmd); cmd[0] = '\0';
}

vector< pair<string, string> > get_negatives(string idx_dir, int num_negatives) {

        // current way of randomly selecting negatives
        // picks the first 10k files from idx_dir, and selects num_negatives at a random
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
                        warc_file = warc_dir.substr(0, warc_dir.size()-4);
                        //warc_file += ".x";
                        negative_cands.push_back(make_pair(offset, warc_file));
                }
                idx_file.close();
        }

        cout << negative_cands.size() << endl;
        vector<unsigned int> indices(negative_cands.size());
        iota(indices.begin(), indices.end(), 0);
        random_shuffle(indices.begin(), indices.end());

        int random_idx = 0;
        int lim = negative_cands.size() > num_negatives ? num_negatives : negative_cands.size();
        while (negatives.size() < lim) {
                negatives.push_back(negative_cands[indices[random_idx++]]);
        }
        return negatives;
}


// Adds random 100 negatives, then writes out current positives + negatives to the training files
int BMI_disk::write_training_files() {


	string idx_dir =  "/zpipe/sample.idx.all";

        // First, we get a random 100 negatives and overwrite the first 100 indices with them
        // Offset, warc file pairs
        vector< pair<string, string> > random_neg_pairs = get_negatives(idx_dir, random_negatives_size);
        for (int i = random_negatives_index; i < random_negatives_size; i++) {
		string warc_file = random_neg_pairs[i].second;
		string offset = random_neg_pairs[i].first;
		extract_txt_files(warc_file, offset);
                string dest = warc_file + ".idx.dir/" + offset; // + ".txt.1"; this part left commented for searching 
                negatives[i] = dest;
        }
	
	// Prepare the pos and neg files
        ofstream negfiles, posfiles;
        negfiles.open(session_dir + "negfiles");
        for (auto neg : negatives) {
		if (!neg.empty()) {
                	negfiles << neg << endl;
		}
        }
        negfiles.close();

        posfiles.open(session_dir + "posfiles");
	for (auto pos : positives) {
		posfiles << pos << endl;
	}
        posfiles.close();
	return 0;
}

int BMI_disk::init_misinfo_qrels() {


        // Temp testing, delete afterwards
        /*batch_mps = {{0, 0.65}, {0, 0.4}, {0, 0.35}, {0, 0.2}, {0, 0.1}, {0, 0.25}, {0, 0.05}, {0, 0.2}, {0,0.1}, {0, 0}, {0, 0}, {0, 0.1}, {0, 0.1}, {0, 0.1}, {0, 0}};

        if(is_stopping_point()) {
	    std::cerr << "Stop Norway Spruce\n" << std::endl;
        }
        batch_mps.clear(); 
   
        batch_mps = {{0, 0.45}, {0, 0}, {0, 0.05}, {0, 0}, {0, 0}, {0, 0.1}, {0, 0.2}, {0, 0.75}, {0, 0.45}, {0, 0.35}, {0, 0.15}, {0, 0.05}, {0, 0.1}, {0, 0.05}, {0, 0.1}};
        if(is_stopping_point()) {
	     std::cerr << "Stop American War\n" << std::endl;
        }
        batch_mps.clear(); 

        batch_mps = {{0, 0.8}, {0, 0.2}, {0, 0}, {0, 0.15}, {0, 0.05}, {0, 0}, {0, 0.05}, {0, 0.05}, {0, 0}, {0, 0.05}};// {0, 0}, {0, 0}, {0, 0}, {0, 0.15}, {0, 0.2}};
        if(is_stopping_point()) {
	     std::cerr << "Stop Halloween\n" << std::endl;
        }  
        batch_mps.clear(); */
        // Testing ends

	string qrels_path = "/data/misinfo/2019qrels_relevance.custom.txt";

        ifstream qrels_file(qrels_path);
        string line;
        if (qrels_file.is_open()){
                while (getline(qrels_file, line)){
			
			int topic, zz, relevance;
			string doc_id;
		
                        stringstream ssline(line);
                        ssline >> topic >> zz >> doc_id >> relevance;
			if (relevance != 0 ) {
				qrels[topic].push_back(doc_id);
			}
                }
                qrels_file.close();
        }

	/*for (int i = 1; i <=10 ; i++){
		std::cerr << i << " " << qrels[i].size() << std::endl;
		for(auto id : qrels[i]) {
			std::cerr << id << " ";
		}
		std::cerr << std::endl;
	}*/

	return 0;
}

vector<string> tokenize(string s, string delimiter){
        //std::string s = "scott>=tiger>=mushroom";
        //std::string delimiter = ">=";
        vector<string> tokens;
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

void extract_seed_positive(string clueweb_id, string session_dir) {

        vector<string> split_id = tokenize(clueweb_id, "-");
        string dir, file, id;
        dir = split_id[1]; file = split_id[2]; id = split_id[3];
        
        char cmd[500];
        sprintf(cmd, "/zpipe/extract-seed-docs.sh %s %s %s %s", dir.c_str(), file.c_str(), id.c_str(), session_dir.c_str());
        system(cmd); cmd[0] = '\0';
}

// Initialize positives and negatives with seed query, seed judgements and make space for random negatives
int BMI_disk::init_training_data() {

        // First, we make space for the random 100 negatives
	// Or, maybe not - just decided to add at the end for now

	random_negatives_index = negatives.size();
        for (int i = random_negatives_index; i < random_negatives_size; i++) {
                negatives.push_back(""); // Put an empty string to reserve space. Note that while writing to file we ignore these if found.
        }

	// Next, put the seed query in the positives
        ofstream seeddoc;
        seeddoc.open(session_dir + "seeddoc.txt");
        seeddoc << seed_query << endl;
        seeddoc.close();
	positives.push_back(session_dir + "seeddoc.txt");

        // Next, call the extract seed docs script for each seed positive
        for (auto cw_id : seed_positives) {
                cerr << "Extracting " << cw_id << endl;
		extract_seed_positive(cw_id, session_dir);
        }

	// Next, read the seed positives into the positives vector.
        ifstream seedpos_file(session_dir + "seed.posfiles");
        string line;
        if (seedpos_file.is_open()){
                while (getline(seedpos_file, line)){
                        cerr << "Seed pos txt- " << line << endl;
                        positives.push_back(line);
                        judgments[line] = 1; // Marking seed positives as rel
                }
                seedpos_file.close();
        }

	return 0;
}

int do_training(string session_dir) {

        // Use tarall to calculate concordance for current training files
        char cmd[500];
        const char * cbase_dir = session_dir.c_str();
        sprintf(cmd, "tar cf - -T %snegfiles | /zpipe/tarall /dev/stdin 1> %snegatives.conc 2> %sneg.stderr", cbase_dir, cbase_dir, cbase_dir);
        system(cmd); cmd[0] = '\0';
        sprintf(cmd, "tar cf - -T %sposfiles | /zpipe/tarall /dev/stdin 1> %spositives.conc 2> %spos.stderr", cbase_dir, cbase_dir, cbase_dir);
        system(cmd); cmd[0] = '\0';

        // Use readdf to get svm.fil from concordance (uses the global df and N)
        sprintf(cmd, "/zpipe/readdf /zpipe/cw12-df/df.small /zpipe/cw12-df/dfN.all < %snegatives.conc > %snegatives.svm.fil", cbase_dir, cbase_dir);
        system(cmd); cmd[0] = '\0';

        sprintf(cmd, "/zpipe/readdf /zpipe/cw12-df/df.small /zpipe/cw12-df/dfN.all < %spositives.conc > %spositives.svm.fil", cbase_dir, cbase_dir);
        system(cmd); cmd[0] = '\0';

        // Label positives and negatives, concat into trainset
        sprintf(cmd, "cat %snegatives.svm.fil | awk '{printf -1; printf \" \"; for (i=2; i<NF; i++) printf $i \" \"; print $NF}' > %snegatives.trainset", cbase_dir, cbase_dir);
        system(cmd); cmd[0] = '\0';

        sprintf(cmd, "cat %spositives.svm.fil | awk '{printf 1; printf \" \"; for (i=2; i<NF; i++) printf $i \" \"; print $NF}' > %spositives.trainset", cbase_dir, cbase_dir);
        system(cmd); cmd[0] = '\0';

        sprintf(cmd, "cat %spositives.trainset %snegatives.trainset > %sall.trainset", cbase_dir, cbase_dir, cbase_dir);
        system(cmd); cmd[0] = '\0';

        // Then we train using sofia-ml
        sprintf(cmd, "/zpipe/sofia-ml-read-only/src/sofia-ml --learner_type logreg-pegasos --loop_type roc --lambda 0.1 --iterations 200000 --training_file %sall.trainset --dimensionality 800000000 --model_out %sxtest_model", cbase_dir, cbase_dir);
        system(cmd); cmd[0] = '\0';

        return 0;
}

void add_doc_ids_from_scores_file(string session_dir, int current_round, string scores_file_name, vector< pair<string, double> > &doc_ids) {

	string scores_path = session_dir + scores_file_name;
	int target_size = doc_ids.size() + 1000; // We want to add atmost 1000 more from the current scores file

        ifstream scores_file(scores_path);
        string line;
        if (scores_file.is_open()){
                while (doc_ids.size() < target_size){  // This is the number of docs scored
                        getline (scores_file, line);
                        if (line.find("/zpipe/training") == string::npos) { // HARD-CODING ALERT
                                continue; // if the line isn't a score from the session model
                        }
                        string m, model_path, rank, binx_path, offset;
			double score;
                        stringstream ssline(line);

                        ssline >> m >> model_path >> rank >> score >> binx_path >> offset;

                        //string gvc12 = "gvc-12";
                        //string cw_binx = "cw-12-binx";
                        //string svm_binx = ".svm.binx";
                        //string idx_dir = ".idx.dir/";
                        string suffix = ".new.svm.binx." + to_string(current_round);
                        string warc_file = binx_path.substr(0, binx_path.size()-suffix.size());
                        //binx_path.replace(binx_path.find(cw_binx), cw_binx.length(), gvc12);
                        //binx_path.replace(binx_path.find(svm_binx), svm_binx.length(), idx_dir);
			
			extract_txt_files(warc_file, offset);
			string doc_id = warc_file + ".idx.dir/" + offset; // + ".txt.1"; preserved as comment for searching
                        doc_ids.push_back(make_pair(doc_id, score));
                }
                scores_file.close();
        }

}

bool score_compare(const pair<string, double> &a, const pair<string, double> &b) {
	return a.second > b.second; // to sort scores in decreasing order
}

vector<string> get_docs_to_judge(string session_dir, int current_round, int max_count) {

        vector< pair<string, double> > doc_ids;

	add_doc_ids_from_scores_file(session_dir, current_round, "scores.all.txt", doc_ids);
	//add_doc_ids_from_scores_file(session_dir, "scores2.txt", doc_ids);
	//add_doc_ids_from_scores_file(session_dir, "scores3.txt", doc_ids);
	
	sort(doc_ids.begin(), doc_ids.end(), score_compare); // Decreasing order of scores
	
        vector<string> top_doc_ids;

        max_count = max_count < doc_ids.size() ? max_count : doc_ids.size(); // Limit max_count to number of docs scored
	for(int i = 0; i < max_count; i++) {
                top_doc_ids.push_back(doc_ids[i].first);
        }
	reverse(top_doc_ids.begin(), top_doc_ids.end()); // This is required bcoz the internal judgement_queue is dequeued in reverse
        return top_doc_ids;
}

string build_score_cmd(int round, string session_dir) {

        vector<string> disks{"sdaa1", "sdab1", "sdac1", "sdad1", "sdae1", "sdaf1", "sdag1", "sdah1", "sdai1", "sdaj1", "sdak1", "sdal1", "sdc1", "sdd1", "sde1", "sdf1", "sdg1", "sdh1", "sdi1", "sdj1", "sdk1", "sdl1", "sdm1", "sdn1", "sdo1", "sdp1", "sdq1", "sdr1", "sds1", "sdt1", "sdu1", "sdv1", "sdw1", "sdx1", "sdy1", "sdz1"};
        string cmd = "/zpipe/myreadT3 " + session_dir + "xtest_model";
        for (auto d : disks) {
                cmd += " - /mnt/a536sing/" + d + "/Disk*/*/*.binx." + to_string(round);
        }
        cmd += " > " + session_dir + "scores.all.txt";
        cmd += " 2> " + session_dir + "scores.stderr";
        return cmd;
}

vector<string> do_rescoring(string session_dir, int current_round) {
 
	char cmd[2500];
        const char * cbase_dir = session_dir.c_str();
        vector <string> top_docs;
 
	// Convert the model into myreadT format and run myreadT
        //sprintf(cmd, "cd %s; for x in test_model* ; do  (tr ' ' '\n' < $x | cat -n | sed -e '/\t0$/d' > x$x ); done", cbase_dir);
        //system(cmd); cmd[0] = '\0';

	string cmd_str = build_score_cmd(current_round, session_dir);
        strcpy(cmd, cmd_str.c_str());
        system(cmd); cmd[0] = '\0';
        
	top_docs = get_docs_to_judge(session_dir, current_round, 1000);
	return top_docs;
}

/*
vector<float> BMI_disk::train(){
    uniform_int_distribution<size_t> distribution(0, documents->size()-1);
    for(int i = 0;i<random_negatives_size;i++){
        size_t idx = distribution(rand_generator);
        negatives[random_negatives_index + i] = &documents->get_sf_sparse_vector(idx);
    }

    std::cerr<<"Training on "<<positives.size()<<" +ve docs and "<<negatives.size()<<" -ve docs"<<std::endl;
    
    return LRPegasosClassifier(training_iterations).train(positives, negatives, documents->get_dimensionality());
}
*/

vector<string> BMI_disk::get_doc_to_judge(uint32_t count=1){
    while(true){
        {
            lock_guard<mutex> lock_judgment_list(judgment_list_mutex);
            lock_guard<mutex> lock_judgments(training_cache_mutex);

            if(!judgment_queue.empty()){
                vector<string> ret;
                for(int i = judgment_queue.size()-1; i>=0 && judgment_queue[i] != "" && ret.size() < count; i--){
                    if(!is_judged(judgment_queue[i]))
                        ret.push_back(judgment_queue[i]);
                    else {
                        judgment_queue.erase(judgment_queue.begin() + i);
                    }
                }
                return ret;
            }
        }
        this_thread::sleep_for(chrono::milliseconds(100));
    }
}

void BMI_disk::add_to_judgment_list(const vector<string> &ids){
    lock_guard<mutex> lock(judgment_list_mutex);
    if(ids.size() == 0)
        judgment_queue = {""};
    else
        judgment_queue = ids;
}

void BMI_disk::add_to_training_cache(string id, int judgment){
    lock_guard<mutex> lock(training_cache_mutex);
    training_cache[id] = judgment;
}

void BMI_disk::record_judgment_batch(vector<pair<string, int>> _judgments){
    for(const auto &judgment: _judgments){
        add_to_training_cache(judgment.first, judgment.second);
    }

    if(!async_mode){
        if(get_judgments_size() + training_cache.size() >= state.next_iteration_target)
            perform_iteration();
    }else{
        auto t = thread(&BMI_disk::perform_iteration_async, this);
        t.detach();
    }
}

void BMI_disk::record_judgment(string doc_id, int judgment){
    record_judgment_batch({{doc_id, judgment}});
}

// Syncs training cache 
// In batch mode, the training cache *is* the previous batch
// So this function also decides whether to increment the downsampling round variable
// This function also increments the estimate of total no. of rel docs
void BMI_disk::sync_training_cache() {
    lock_guard<mutex> lock(training_cache_mutex);
    std::cerr << "Start syncing session " << session_id << " (Seed- " << seed_query << ")" << std::endl;
    std::cerr << "Found " << training_cache.size() << " new judgements!" << std::endl;
    int cache_positives = 0;
    int cache_negatives = 0;
    for(pair<string, int> training: training_cache){
        if(judgments.find(training.first) != judgments.end()){
            std::cerr<<"Rewriting judgment history"<<std::endl;
            if(judgments[training.first] > 0){
                for(int i = (int)positives.size() - 1; i > 0; i--){
                    if(positives[i] == training.first){
                        positives.erase(positives.begin() + i);
                        break;
                    }
                }
            } else {
                for(int i = (int)negatives.size() - 1; i > 0; i--){
                    if(negatives[i] == training.first){
                        negatives.erase(negatives.begin() + i);
                        break;
                    }
                }
            }
        }

        judgments[training.first] = training.second;

        if(training.second > 0) {
            positives.push_back(training.first);
            cache_positives++;
        }
        else{
            negatives.push_back(training.first);
            cache_negatives++;
        }
    }

    float batch_mp = cache_positives * 1.0 / judgments_per_iteration;
    batch_mps.push_back({current_round, batch_mp});
    string stop_marker;
    if (is_stopping_point()) {
        stop_marker = "STOP LABELING";
        std::cerr<<"\nSTOP LABELING\n Stopping condition for labeling reached\n" << std::endl;
    } else {
        stop_marker = "CONTINUE LABELING";
	std::cerr<<"\nCONTINUE LABELING\n Condition not reached, continue labeling\n" << std::endl;
    }

    total_rel += cache_positives * pow(2, current_round-1);
    std::cerr<<"Total relevant estimate - " << total_rel << std::endl;
    std::cerr<<"Current sampling round - " << current_round << std::endl;
    ofstream totalfile;
    totalfile.open(session_dir + "total.N", std::ios_base::app);
    totalfile << cache_positives << " " << current_round << " " << total_rel << " ";
    totalfile << stop_marker << std::endl;
    totalfile.close();

    round_positives += cache_positives; // number of positives in the current round

    if(training_cache.size() > 0) { // to skip over initial training with zero judgements
    	round_cnt += 1; // number of batches in the current round
    }

    if (positives.size() - 1 >= enough_rel) {
        if (cache_positives >= batch_mp_cutoff * judgments_per_iteration) {
            current_round++;
            enough_rel = 2 * enough_rel;

            float round_avg_mp = round_positives / (round_cnt * judgments_per_iteration);
  
            // double down-sampling condition
            if (round_avg_mp >= round_mp_cutoff && round_cnt >= round_cnt_cutoff) {
                 std::cerr<<"Double downsampling, skipping round " << current_round << std::endl;
                 current_round++; // Double down-sampling condition is met
            }
            round_positives = 0;
            round_cnt = 0;
            std::cerr<<"Moving to sampling round " << current_round << std::endl;
        }
    }
   
    training_cache.clear();
    std::cerr << "Done syncing session " << session_id << " (Seed- " << seed_query << ")" << std::endl;
}

// Called once in perform_iteration
vector<string> BMI_disk::perform_training_iteration(){
    lock_guard<mutex> lock_training(training_mutex);

    sync_training_cache();

    // Training
    TIMER_BEGIN(training);
    write_training_files();
    do_training(session_dir);
    TIMER_END(training);

    // Scoring
    TIMER_BEGIN(rescoring);
    vector<string> results; // Empty for now, do_rescoring should populate 
    results = do_rescoring(session_dir, current_round);
    TIMER_END(rescoring);

    return results;
}

bool BMI_disk::is_stopping_point(){
    int sz = batch_mps.size();
    if (sz >= 15) {
        for (int part = 5; part < sz - 5 + 1; part++) {
	    int rest = sz - part;
            float avg1 = 0; float avg2 = 0;
 	    for(int i = 0; i < part; i++) {
		avg1 += batch_mps[i].second;
	    }
            avg1 = avg1 / part;
            for(int i = part; i < sz; i++) {
	        avg2 += batch_mps[i].second;
	    }
            avg2 = avg2 / rest;
            if (avg1 > 5 * avg2) return true;
	}
    }
    return false;
}

/*
std::vector<std::pair<string, float>> BMI_disk::get_ranklist(){
    vector<std::pair<string, float>> ret_results;
    auto w = train();
    for(uint32_t i = 0; i < documents->size(); i++){
        ret_results.push_back({documents->get_id(i), documents->inner_product(i, w)});
    }
    sort(ret_results.begin(), ret_results.end(), [] (const pair<string, float> &a, const pair<string, float> &b) -> bool {return a.second > b.second;});
    return ret_results;
*/
