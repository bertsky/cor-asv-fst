#include <fst/fstlib.h>
#include <stdio.h>
#include <iterator>
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <mutex>
#include <pthread.h>
#include <condition_variable>

#include <locale>
#include <codecvt>
#include <string>

#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <cerrno>

//#include <cunistd>
#include <sys/time.h>
#include <sys/resource.h>
#include <ctime>


//g++ -std=c++14 -I /usr/local/include -ldl -lfst -lpthread experiments.cpp -o experiments


using namespace fst;
using namespace std;


pid_t child_pid;
int status;

struct rusage usage;


void kill_child(int sig) {

    kill(child_pid, SIGTERM);
}


StdVectorFst create_input_transducer(string word, const SymbolTable* table) {

    StdVectorFst input;
    input.SetInputSymbols(table);
    input.SetOutputSymbols(table);
    input.AddState();
    input.SetStart(0);


    wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    //string narrow = converter.to_bytes(wide_utf16_source_string);
    wstring wide = converter.from_bytes(word);
    cout << "Word: " << converter.to_bytes(wide) << endl;

    for (int i=0; i < wide.length(); i++) {

        //cout << converter.to_bytes(wide[i]) << endl;
        //char letter =  word[i];
        string narrow_symbol = converter.to_bytes(wide[i]);

        int label = table->Find(narrow_symbol);
        //cout << label << endl;
        input.AddArc(i, StdArc(label, label, 0, i+1));
        input.AddState();

    }
    input.SetFinal(wide.length(), 0);
    return input;
}


ComposeFst<StdArc> lazy_compose(
    StdVectorFst *input1,
    StdVectorFst *input2,
    StdVectorFst *input3,
    string word) {

    const SymbolTable table = *(input1->OutputSymbols());
    StdVectorFst input_transducer = create_input_transducer(word, &table);

    input_transducer.Write(string("input_") + word + string(".fst"));

    ArcSort(input1, StdOLabelCompare());
    //ArcSort(input2, StdILabelCompare());

    ComposeFst<StdArc> delayed_result(*input1, *input2);

    if (input3) {
        ArcSort(input3, StdILabelCompare());
        ComposeFst<StdArc> delayed_result2(delayed_result, *input3);
    }
    
    ArcSort(&input_transducer, StdOLabelCompare());
    ComposeFst<StdArc> delayed_result2(input_transducer, (input3 ? delayed_result2 : delayed_result));

    return delayed_result2;
}


StdVectorFst eager_compose(
    StdVectorFst *input1,
    StdVectorFst *input2,
    StdVectorFst *input3,
    string word) {

    const SymbolTable table = *(input1->OutputSymbols());
    StdVectorFst input_transducer = create_input_transducer(word, &table);

    input_transducer.Write(string("input_") + word + string(".fst"));

    StdVectorFst result;

    ArcSort(&input_transducer, StdOLabelCompare());

    Compose(input_transducer, *input1, &result);

    if (input2) {
        ArcSort(&result, StdOLabelCompare());
        Compose(result, *input2, &result);
    }

    if (input3) {
        ArcSort(&result, StdOLabelCompare());
        Compose(result, *input3, &result);
    }
    
    return result;
}


float get_cpu_time() {

    if (getrusage(RUSAGE_SELF, &usage) < 0) {
        std::perror("cannot get usage statistics");
        // exit(1);
        return -1;
    } else {
    
        return usage.ru_utime.tv_sec + usage.ru_stime.tv_sec +
            1e-6*usage.ru_utime.tv_usec + 1e-6*usage.ru_stime.tv_usec;
    
    }
}


StdVectorFst compose_and_search(
    StdVectorFst *input1,
    StdVectorFst *input2,
    StdVectorFst *input3,
    string word,
    bool lazy,
    int nbest) {

    StdVectorFst nbest_transducer;

    if (lazy) {

        ComposeFst<StdArc> composed = lazy_compose(input1, input2, input3, word);

        float compose_time = get_cpu_time();
        cout << "Composition: " << compose_time << endl;

        ShortestPath(composed, &nbest_transducer, nbest);

        float total_time = get_cpu_time();
        float search_time = total_time - compose_time;
        cout << "Search: " << search_time << endl;
        cout << "Total time: " << total_time << endl;

    }
    else {

        StdVectorFst composed = eager_compose(input1, input2, input3, word);

        float compose_time = get_cpu_time();
        cout << "Composition: " << compose_time << endl;

        ShortestPath(composed, &nbest_transducer, nbest);

        float total_time = get_cpu_time();
        float search_time = total_time - compose_time;
        cout << "Search: " << search_time << endl;
        cout << "Total time: " << total_time << endl;

    }


    if (getrusage(RUSAGE_SELF, &usage) < 0) {
        std::perror("cannot get usage statistics");
        // exit(1);
    } else {
    
        // maximum resident set size in kB
        std::cout << "RAM usage: " << usage.ru_maxrss << endl;
    
    }


    return nbest_transducer;
}


StdVectorFst composition_wrapper(
    StdVectorFst *input1,
    StdVectorFst *input2,
    StdVectorFst *input3,
    string word,
    bool lazy,
    int nbest) {

    mutex m;
    condition_variable cv;
    StdVectorFst return_value;

    thread t([&m, &cv, &return_value, input1, input2, input3, word, lazy, nbest]() {

        return_value = compose_and_search(input1, input2, input3, word, lazy, nbest);
        cv.notify_one();
    });

    t.detach();

    {
        unique_lock<mutex> l(m);
        if(cv.wait_for(l, 10s) == cv_status::timeout) 
            throw runtime_error("Timeout");
    }

    return return_value;    
}


StdVectorFst perform_experiment(
    StdVectorFst *input1,
    StdVectorFst *input2,
    StdVectorFst *input3,
    string word,
    bool lazy,
    int nbest) {

    StdVectorFst nbest_transducer;

    bool timedout = false;
    try {
        nbest_transducer = composition_wrapper(input1, input2, input3, word, lazy, nbest);
    }
    catch(std::runtime_error& e) {
        cout << e.what() << endl;
        timedout = true;
    }

    if(!timedout)
        cout << "Success" << endl;

    return nbest_transducer;

}

int main(int argc, const char* argv[]) {

    // register signal and signal handler

    // signal(SIGALRM,(void (*)(int))kill_child);

    // read transducer files

    chrono::steady_clock::time_point begin = chrono::steady_clock::now();

    StdVectorFst *error_model1 = StdVectorFst::Read("error_transducers/max_error_1.ofst");
    StdVectorFst *error_model2 = StdVectorFst::Read("error_transducers/max_error_2.ofst");
    StdVectorFst *error_model3 = StdVectorFst::Read("error_transducers/max_error_3.ofst");
    StdVectorFst *error_model4 = StdVectorFst::Read("error_transducers/max_error_4.ofst");
    StdVectorFst *error_model5 = StdVectorFst::Read("error_transducers/max_error_5.ofst");

    StdVectorFst *context_error_model1 = StdVectorFst::Read("context/max_error_1_context_2_3.ofst");
    StdVectorFst *context_error_model2 = StdVectorFst::Read("context/max_error_2_context_2_3.ofst");
    StdVectorFst *context_error_model3 = StdVectorFst::Read("context/max_error_3_context_2_3.ofst");
    StdVectorFst *context_error_model4 = StdVectorFst::Read("context/max_error_4_context_2_3.ofst");
    StdVectorFst *context_error_model5 = StdVectorFst::Read("context/max_error_5_context_2_3.ofst");

    StdVectorFst *context_error_model_2_1 = StdVectorFst::Read("context/max_error_1_context_2.ofst");
    StdVectorFst *context_error_model_2_2 = StdVectorFst::Read("context/max_error_2_context_2.ofst");
    StdVectorFst *context_error_model_2_3 = StdVectorFst::Read("context/max_error_3_context_2.ofst");
    StdVectorFst *context_error_model_2_4 = StdVectorFst::Read("context/max_error_4_context_2.ofst");
    StdVectorFst *context_error_model_2_5 = StdVectorFst::Read("context/max_error_5_context_2.ofst");

    StdVectorFst *context_error_model_3_1 = StdVectorFst::Read("context/max_error_1_context_3.ofst");
    StdVectorFst *context_error_model_3_2 = StdVectorFst::Read("context/max_error_2_context_3.ofst");
    StdVectorFst *context_error_model_3_3 = StdVectorFst::Read("context/max_error_3_context_3.ofst");
    StdVectorFst *context_error_model_3_4 = StdVectorFst::Read("context/max_error_4_context_3.ofst");
    StdVectorFst *context_error_model_3_5 = StdVectorFst::Read("context/max_error_5_context_3.ofst");

    StdVectorFst *lexicon_small = StdVectorFst::Read("lexicon_transducers/lexicon.ofst");
    StdVectorFst *lexicon_big = StdVectorFst::Read("lexicon_transducers/lexicon_transducer_asse_minimized.ofst");

    StdVectorFst *extended_lexicon_small = StdVectorFst::Read("extended_lexicon/extended_lexicon.ofst");
    StdVectorFst *rules = StdVectorFst::Read("morphology/rules.ofst");

    chrono::steady_clock::time_point end = chrono::steady_clock::now();
    //cout << "Load Transducers: " << chrono::duration_cast<chrono::microseconds>(end - begin).count() << endl;

    vector<StdVectorFst*> error_models = {error_model1, error_model2, error_model3, error_model4, error_model5};
    vector<StdVectorFst*> context_error_models_23 = {context_error_model1, context_error_model2, context_error_model3, context_error_model4, context_error_model5};
    vector<StdVectorFst*> context_error_models_2 = {context_error_model_2_1, context_error_model_2_2, context_error_model_2_3, context_error_model_2_4, context_error_model_2_5};
    vector<StdVectorFst*> context_error_models_3 = {context_error_model_3_1, context_error_model_3_2, context_error_model_3_3, context_error_model_3_4, context_error_model_3_5};
    // parameters for experiments

    int num_errors = 1;
    //StdVectorFst *error_model = error_model3;

    bool use_morphology = true;
    bool big_lexicon = true;

    bool extended_lexicon = false; // lexicon + morphology, only for use of morphology
    bool precomposed = false; // error + lexicon, not in combination with extended_lexicon
    int nbest = 1;
    bool lazy = false;
    int context = 123;

    //string input_words[] = {"bIeibt", "zuständigen", "miüssen", "radioalctiver",
    //    "schiefßen", "niedersBchsischen"};

    vector<string> input_words = {"bIeibt", "zuständigen", "miüssen", "radioalctiver",
        "schiefßen", "niedersBchsischen"};

    // select lexicon

    StdVectorFst *lexicon;
    if (big_lexicon) {
        lexicon = lexicon_big;
    }
    else {
        lexicon = lexicon_small;
    }

    if (extended_lexicon and !big_lexicon) {
        lexicon = extended_lexicon_small;
    }

    // select error models

    switch (context) {
        case 23 :
            error_models = context_error_models_23;
            break;
        case 2 :
            error_models = context_error_models_2;
        case 3 :
            error_models = context_error_models_3;
        default : 
            break;
    }
    
    // adjust output and input symbol tables for composition

    bool relabel;

    const SymbolTable output_symbols = *(error_models[0]->OutputSymbols());
    const SymbolTable input_symbols = *(lexicon->InputSymbols());

    SymbolTable lexicon_new = *(MergeSymbolTable(output_symbols, input_symbols, &relabel));

    const SymbolTable rules_symbols = *(rules->InputSymbols());

    lexicon_new = *(MergeSymbolTable(lexicon_new, rules_symbols, NULL));
    //const combined_lexicon_new = *(MergeSymbolTable(lexicon_new, rules_symbols, NULL));

    if (relabel) {
      Relabel(lexicon, &lexicon_new, &lexicon_new);
      //Relabel(error_model, &lexicon_new, nullptr);
      Relabel(rules, &lexicon_new, &lexicon_new);
    }

    lexicon->SetOutputSymbols(&lexicon_new);
    lexicon->SetInputSymbols(&lexicon_new);

    rules->SetOutputSymbols(&lexicon_new);
    rules->SetInputSymbols(&lexicon_new);


    for (int i = 0; i < std::end(error_models) - std::begin(error_models); i++) {
        error_models[i]->SetOutputSymbols(&lexicon_new);
        error_models[i]->SetInputSymbols(&lexicon_new);
    }

    //error_model->SetOutputSymbols(&lexicon_new);
    //error_model->SetInputSymbols(&lexicon_new);


    // use morphology?
    if (!use_morphology) {
        rules = NULL;
    }


    // compose and search


    StdVectorFst nbest_transducer;

    vector<int> error_numbers = {1, 2, 3, 4, 5};

    for (int h = 0; h < std::end(error_numbers) - std::begin(error_numbers); h++) {

        //cout << "Number of errors: " << error_numbers[h] << endl;

        vector<int> nbests = {1, 10, 50};

        for (int j = 0; j < std::end(nbests) - std::begin(nbests); j++) {

            //cout << "n-best: " << nbests[j] << endl;
            //cout << endl;

            for (int i = 0; i < std::end(input_words) - std::begin(input_words); i++) {

                string word = input_words[i];

                cout << "Context: " << context << endl;

                cout << "Number of errors: " << error_numbers[h] << endl;
                cout << "n-best: " << nbests[j] << endl;

                cout << "Lazy: " << lazy << endl;
                cout << "Morphology: " << use_morphology << endl;
                cout << "Asse Lexicon: " << big_lexicon << endl;

                if ((child_pid = fork()) < 0) {
                
                    std::perror("fork() failed");
                
                }
                else if (child_pid == 0) {
                
                    //std::cout << "starting child " << i << endl;
                    alarm(10); // give up after 10s

                    nbest_transducer = compose_and_search(
                        error_models[error_numbers[h] - 1],
                        lexicon,
                        rules,
                        word,
                        lazy,
                        nbests[j]);

                    nbest_transducer.Write(word + string(".fst"));

                    cout << "States: " << nbest_transducer.NumStates() << endl;

                    exit(0);
                
                 }
                 else {
                
                    if (wait(&status) < 0) {
                
                        std::perror("wait() failed");
                    }
                    else {
                
                        if (WIFEXITED(status)) 
                            //std::cout << "child " << i << " exited with " << WEXITSTATUS(status) << endl;
                            std::cout << "Timeout: 0" << endl;
                
                        else if (WIFSIGNALED(status))
                            //std::cout << "child " << i << " aborted after signal " << WTERMSIG(status) << endl;
                            std::cout << "Timeout: 1" << endl;
                    }

                    cout << endl;
                
                }

            }
        }
    }

    //bool timedout = false;
    //try {
    //    nbest_transducer = composition_wrapper(error_model, lexicon, NULL, word, lazy, 1);
    //}
    //catch(std::runtime_error& e) {
    //    std::cout << e.what() << std::endl;
    //    timedout = true;
    //}

    //if(!timedout)
    //    std::cout << "Success" << std::endl;


    // project output

    //Project(&nbest_transducer, PROJECT_OUTPUT);

    //nbest_transducer.Write("result.fst");


    return 0;

}
