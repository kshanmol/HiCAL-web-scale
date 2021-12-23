#include <random>
#include <mutex>
#include <set>
#include <map>
#include "dataset.h"

class BMI_disk{
    protected:

    string session_id;
    string base_dir = "/zpipe/training/";
    string session_dir;
    int current_round = 1; // Downsampling
    int batch_size = 20;
    int enough_rel = 25; // This is hyperparameter T which determines when to sample
    float batch_mp_cutoff = 0.1;
    int total_rel = 0;
    std::vector<std::pair<int, float>> batch_mps;

    float round_mp_cutoff = 0.5; // Min average mp for double downsampling
    int round_cnt_cutoff = 3; // Min rounds for double downsampling
    float round_positives = 0; // Numnber of positives so far in current round
    int round_cnt = 0; // Number of batches so far in current round

    // Number of threads used for rescoring docs
    int num_threads;

    // Number of judgments to do before training weights
    int judgments_per_iteration;

    int training_iterations;

    int extra_judgment_docs = 50;

    // Async Mode
    bool async_mode;

    // If true, the judgments_per_iteration grows with every iteration
    bool is_bmi;

    // The current state of CAL
    struct State{
        uint32_t cur_iteration = 0;
        uint32_t next_iteration_target = 0;
        bool finished = false;
    }state;

    // Stores an ordered list of documents to judge based on the classifier scores
    std::vector<string> judgment_queue;

    // rand() shouldn't be used because it is not thread safe
    std::mt19937 rand_generator;

    // Current of dataset being used to train the classifier
    const string seed_query;
    vector<string> seed_positives;
    std::map<string, int> judgments;
    vector<string> positives, negatives;
    int random_negatives_index;
    int random_negatives_size = 100;

    // Whenever judgements are received, they are put into training_cache,
    // to prevent any race condition in case training_data is being used by the
    // classifier
    std::unordered_map<string, int> training_cache;

    // Mutexes to control access to certain objects
    std::mutex judgment_list_mutex;
    std::mutex training_mutex;
    std::mutex async_training_mutex;
    std::mutex training_cache_mutex;
    std::mutex state_mutex;

    // (For now, specific to Medical Misinfo, ClueWeb12) Stores the list of relevant documents for each topic number
    // Helps with evaluation wrt qrels
    std::map<int, vector<string>> qrels;

    // Tasks to perform in order to finish the session
    void finish_session();
    bool try_finish_session();

    // train using the current training set and assign the weights to `w`
    // return success or failure (0/1)
    //virtual int train();

    // Add the ids to the judgment list
    void add_to_judgment_list(const std::vector<string> &ids);

    // Add to training_cache
    void add_to_training_cache(string id, int judgment);

    // Handler for performing an iteration
    void perform_iteration();
    void perform_iteration_async();
    void sync_training_cache();

    public:
    BMI_disk(string session_id,
        string seed_query,
        vector<string> seed_positives,
        int num_threads,
        int judgments_per_iteration,
        bool async_mode,
        int training_iterations,
        bool initialize = true);

    virtual int write_training_files();
    virtual int init_training_data();
    virtual int init_misinfo_qrels();
    // Handler for performing a training iteration
    virtual std::vector<string> perform_training_iteration();

    // Check if a given document is judged, lock training_cache_mutex before using this (not done here for efficiency purpose)
    virtual bool is_judged(string id) {
        return judgments.find(id) != judgments.end() || training_cache.find(id) != training_cache.end();
    }

    // Get upto `count` number of documents from `judgment_list`
    virtual std::vector<std::string> get_doc_to_judge(uint32_t count);

    // Record judgment (-1 or 1) for a given doc_id
    virtual void record_judgment(std::string doc_id, int judgment);

    // Record batch judgments
    virtual void record_judgment_batch(std::vector<std::pair<std::string, int>> judgments);


    // Get ranklist for current classifier state
    //virtual vector<std::pair<string, float>> get_ranklist();

    virtual string get_log() {return "{}";}
    virtual bool is_stopping_point();

    struct State get_state(){
        return state;
    }

    int get_current_iteration(){
        return state.cur_iteration;
    }

    int get_judgments_size(){
        return judgments.size() - seed_positives.size();
    }

    int get_batch_remaining_count(){
        return state.next_iteration_target - get_judgments_size() - training_cache.size();
    }
};
