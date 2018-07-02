import error_transducer as et
import hfst
import math
import libhfst

def read_lexicon(filename):
    """Reads a lexicon of tab-separated word-frequency class pairs."""

    freq_list = []

    with open(filename) as f:
        for line in f.readlines():
            word, frequency_class  = line.strip().split('\t')
            freq_list.append((word, word, float(frequency_class)))

    #print(freq_list[0])
    return freq_list


def transducer_from_list(freq_list):

    lexicon_transducer = hfst.HfstBasicTransducer()
    tok = hfst.HfstTokenizer()

    counter = 0

    for (istr, ostr, weight) in freq_list:
        lexicon_transducer.disjunct(tok.tokenize(istr), weight)
        #acceptor = hfst.regex(istr.replace('.', '%.') + ':' + ostr.replace('.', '%.') + '::' + str(weight))
        #lexicon_transducer.disjunct(acceptor)

        counter += 1
        if counter % 10000 == 0:
            print(counter)

    return lexicon_transducer


def optimize_error_transducer(error_transducer):
    error_transducer.minimize()
    #error_transducer.repeat_star()
    error_transducer.remove_epsilons()
    error_transducer.push_weights_to_start()


#def save_transducer(filename, transducer):
#    ostr = hfst.HfstOutputStream(filename=filename)
#    ostr.write(transducer)
#    ostr.flush()
#    ostr.close()



def main():

    print("Read Dictionary...")

    #freq_list = read_lexicon("dictionary1m_wsl_AD_hkl.txt")
    freq_list = read_lexicon("dta_lexicon.txt")
    #lexicon_transducer = et.transducer_from_list(freq_list, frequency_class=True, identity_transitions=True)

    print("Construct Lexicon Transducer...")

    lexicon_transducer = transducer_from_list(freq_list)

    lexicon_transducer = hfst.HfstTransducer(lexixon_transducer)

    et.save_transducer('lexicon_transducer_dta.hfst', lexicon_transducer)

    #with open('lexicon_transducer_dta.att', 'w') as f:
    #    lexicon_transducer.write_att(f, write_weights=True)

    #print("Optimize Lexicon Transducer...")

    #et.optimize_error_transducer(lexicon_transducer)
    #et.save_transducer('lexicon_transducer_asse_optimized.hfst', lexicon_transducer)


    print("Read Punctuation...")

    freq_list = read_lexicon("dta_punctuation_corrected.txt")
    #lexicon_transducer = et.transducer_from_list(freq_list, frequency_class=True, identity_transitions=True)

    print("Construct Punctuation Transducer...")

    punctuation_transducer = transducer_from_list(freq_list)

    punctuation_transducer = hfst.HfstTransducer(punctuation_transducer)
    et.save_transducer('punctuation_transducer_dta.hfst', punctuation_transducer)

    #et.save_transducer('lexicon_transducer_asse.hfst', lexicon_transducer)
    #with open('punctuation_transducer_dta.att', 'w') as f:
    #    punctuation_transducer.write_att(f, write_weights=True)

if __name__ == '__main__':
    main()
