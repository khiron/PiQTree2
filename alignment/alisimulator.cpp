//
//  alisimulator.cpp
//  model
//
//  Created by Nhan Ly-Trong on 23/03/2021.
//

#include "alisimulator.h"

AliSimulator::AliSimulator(Params *input_params)
{
    params = input_params;
    AliSimulator::initializeIQTreeFromTreeFile();
}

AliSimulator::~AliSimulator()
{
    if (!tree || !(tree->aln)) return;
    
    // delete aln
    delete tree->aln;
    
    // delete tree
    delete tree;
}

/**
*  show all input parameters for AliSim
*/
void AliSimulator::showParameters()
{
    cout << " - Tree filepath: " << params->user_file <<"\n";
    cout << " - Length of output sequences: " << params->alisim_sequence_length <<"\n";
    if (!params->model_name.empty())
        cout << " - Model: " << params->model_name <<"\n";
    cout << " - Number of output datasets: " << params->alisim_dataset_num<<"\n";
    if (params->alisim_ancestral_sequence >= 0)
        cout << " - Ancestral sequence position: " << params->alisim_dataset_num <<"\n";
}

/**
*  initialize an IQTree instance from input file
*/
void AliSimulator::initializeIQTreeFromTreeFile()
{
    tree = new IQTree();
    bool is_rooted = false;
    tree->readTree(params->user_file, is_rooted);
    initializeAlignment();
    initializeModel();
}


/**
*  initialize an Alignment instance for IQTree
*/
void AliSimulator::initializeAlignment()
{
    tree->aln = new Alignment();
    
    // set the seq_type and the maximum number of bases based on the Seq_type
    tree->aln->seq_type = tree->aln->getSeqType(params->sequence_type);
    
    switch (tree->aln->seq_type) {
    case SEQ_BINARY:
        tree->aln->num_states = 2;
        break;
    case SEQ_PROTEIN:
        tree->aln->num_states = 20;
        break;
    case SEQ_MORPH:
        throw "Sorry! SEQ_MORPH is currently not supported";
        break;
    case SEQ_POMO:
        throw "Sorry! SEQ_POMO is currently not supported";
        break;
    default:
        tree->aln->num_states = 4;
        break;
    }
    
    // add all leaf nodes' name into the alignment
    addLeafNamesToAlignment(tree->aln, tree->root, tree->root);
}

/**
*  iteratively add name of all leaf nodes into the alignment instance
*/
void AliSimulator::addLeafNamesToAlignment(Alignment *aln, Node *node, Node *dad)
{
    if (node->isLeaf() && node->name!=ROOT_NAME) {
        aln->addSeqName(node->name);
    }
    NeighborVec::iterator it;
    FOR_NEIGHBOR(node, dad, it) {
        addLeafNamesToAlignment(aln, (*it)->node, node);
    }
}

/**
*  initialize a Model instance for IQTree
*/
void AliSimulator::initializeModel()
{
    tree->aln->model_name = params->model_name;
    ModelsBlock *models_block = readModelsDefinition(*params);
    tree->setParams(params);
    
    tree->initializeModel(*params, tree->aln->model_name, models_block);
}

/**
*  generate an alignment from a tree (model, alignment instances are supplied via the IQTree instance)
*/
void AliSimulator::generateSingleDatasetFromSingleTree(string output_filepath, IntVector ancestral_sequence)
{
    // set ancestral sequence to the root node
    tree->MTree::root->sequence = ancestral_sequence;
    
    // simulate the sequence for each node in the tree by DFS
    simulateSeqsForTree();
    
    // write output to file
    writeSequencesToFile(output_filepath);
}

/**
*  generate mutiple alignments from a tree (model, alignment instances are supplied via the IQTree instance)
*/
void AliSimulator::generateMultipleAlignmentsFromSingleTree()
{
    // Get/Generate the ancestral sequence
    IntVector ancestral_sequence;
    // retrieve the ancestral sequence from input file if its position is specified in the input parameter
    if (params->alisim_ancestral_sequence >= 0)
        ancestral_sequence = retrieveAncestralSequenceFromInputFile(params->alisim_ancestral_sequence);
    
    // iteratively generate multiple datasets for each tree
    for (int i = 0; i < params->alisim_dataset_num; i++)
    {
        // if the ancestral sequence is not specified, randomly generate the sequence
        if (params->alisim_ancestral_sequence < 0)
            ancestral_sequence = generateRandomSequence(params->alisim_sequence_length);
        
        // initialize output_filepath
        std::string output_filepath(params->user_file);
        output_filepath = output_filepath
        +"_"+params->alisim_output_filename
        +"_"+convertIntToString(i)+".phy";
        
        generateSingleDatasetFromSingleTree(output_filepath, ancestral_sequence);
    }
}

/**
*  retrieve the ancestral sequence for the root node from an input file
*/
IntVector AliSimulator:: retrieveAncestralSequenceFromInputFile(int sequence_position)
{
    IntVector sequence;
    
    // read sequences from the input file
    Alignment *aln = new Alignment();
    StrVector sequences;
    int nseq = 0, nsite = 0;
    aln->extractSequences(params->aln_file, params->sequence_type, sequences, nseq, nsite);
    
    // delete aln
    delete aln;
    
    // validate sequence_position
    if (sequence_position > nseq)
        outError("The position of the ancestral sequence (" + convertIntToString(sequence_position) + ") is exceeding the number of sequences in the input file ("+convertIntToString(nseq)+")");
    
    // overwrite the output sequence_length
    params->alisim_sequence_length = nsite;
    string sequence_str = sequences[sequence_position - 1];
    
    sequence.resize(nsite);
    // convert the input sequence into (numerical states) sequence
    for (int i = 0; i < nsite; i++)
        sequence[i] = tree->aln->convertState(sequence_str[i]);
        
    return sequence;
}

/**
*  randomly generate the ancestral sequence for the root node
*/
IntVector AliSimulator::generateRandomSequence(int sequence_length)
{
    // initialize sequence
    IntVector sequence;
    sequence.resize(sequence_length);
    
    // get max_num_bases
    int max_num_states = tree->aln->getMaxNumStates();
    
    // if the Frequency Type is FREQ_EQUAL -> randomly generate each site in the sequence follows the normal distribution
    if (tree->getModel()->getFreqType() == FREQ_EQUAL)
    {
        for (int i = 0; i < sequence_length; i++)
            sequence[i] =  random_int(max_num_states);
    }
    else // otherwise, randomly generate each site in the sequence follows the base frequencies defined by the user
    {
        // get the base frequencies
        double *state_freq = new double[max_num_states];
        
        // get user-defined base frequencies (if any)
        if (tree->getModel()->getFreqType() == FREQ_USER_DEFINED)
            tree->getModel()->getStateFrequency(state_freq);
        else // otherwise, randomly generate the base frequencies
        {
            generateRandomBaseFrequencies(state_freq, max_num_states);
            tree->getModel()->setStateFrequency(state_freq);
        }
        
        // convert the probability matrix into an accumulated probability matrix
        convertProMatrixIntoAccumulatedProMatrix(state_freq, 1, max_num_states);
        
        // randomly generate each site in the sequence follows the base frequencies defined by the user
        for (int i = 0; i < sequence_length; i++)
        sequence[i] =  getRandomItemWithAccumulatedProbabilityMatrix(state_freq, 0, max_num_states);
        
        // delete state_freq
        delete []  state_freq;
    }
    
    return sequence;
}

/**
*  randomly generate the base frequencies
*/
void AliSimulator::generateRandomBaseFrequencies(double *base_frequencies, int max_num_bases)
{
    double sum = 0;
    
    // randomly generate the frequencies
    for (int i = 0; i < max_num_bases; i++)
    {
        base_frequencies[i] = random_double();
        sum += base_frequencies[i];
    }
    
    // normalize the frequencies so that sum of them is 1
    for (int i = 0; i < max_num_bases; i++)
    base_frequencies[i] /= sum;
}

/**
*  simulate sequences for all nodes in the tree
*/
void AliSimulator::simulateSeqsForTree()
{
    // get variables
    int sequence_length = params->alisim_sequence_length;
    ModelSubst *model = tree->getModel();
    int max_num_states = tree->aln->getMaxNumStates();
    
    // initialize trans_matrix
    double *trans_matrix = new double[max_num_states*max_num_states];
    
    // simulate Sequences
    simulateSeqs(sequence_length, model, trans_matrix, max_num_states, tree->MTree::root, tree->MTree::root);
    
    // delete trans_matrix array
    delete[] trans_matrix;
}

/**
*  simulate sequences for all nodes in the tree by DFS
*
*/
void AliSimulator::simulateSeqs(int sequence_length, ModelSubst *model, double *trans_matrix, int max_num_states, Node *node, Node *dad)
{
    // process its neighbors/children
    NeighborVec::iterator it;
    FOR_NEIGHBOR(node, dad, it) {
        
        // compute the transition probability matrix
        model->computeTransMatrix((*it)->length, trans_matrix);
        
        // convert the probability matrix into an accumulated probability matrix
        convertProMatrixIntoAccumulatedProMatrix(trans_matrix, max_num_states, max_num_states);
        
        // estimate the sequence for the current neighbor
        (*it)->node->sequence.resize(sequence_length);
        for (int i = 0; i < sequence_length; i++)
        {
            // iteratively select the state for each site of the child node, considering it's dad states, and the transition_probability_matrix
            int starting_index = node->sequence[i]*max_num_states;
            (*it)->node->sequence[i] = getRandomItemWithAccumulatedProbabilityMatrix(trans_matrix, starting_index, max_num_states);
        }
        
        // browse 1-step deeper to the neighbor node
        simulateSeqs(sequence_length, model, trans_matrix, max_num_states, (*it)->node, node);
    }
}

/**
*  get a random item from a set of items with a probability array
*/
int AliSimulator::getRandomItemWithProbabilityMatrix(double *probability_maxtrix, int starting_index, int num_items)
{
    // generate a random number
    double random_number = random_double();
    
    // select the current state, considering the random_number, and the probability_matrix
    double accummulated_probability = 0;
    for (int i = 0; i < num_items; i++)
    {
        accummulated_probability += probability_maxtrix[starting_index+i];
        if (random_number <= accummulated_probability)
            return i;
    }
    
    // if not found, return -1
    return -1;
}


/**
*  convert an probability matrix into an accumulated probability matrix
*/
void AliSimulator::convertProMatrixIntoAccumulatedProMatrix(double *probability_maxtrix, int num_rows, int num_columns)
{
    for (int r = 0; r < num_rows; r++)
    {
        for (int c = 1; c < num_columns; c++)
        probability_maxtrix[r*num_rows+c] = probability_maxtrix[r*num_rows+c] + probability_maxtrix[r*num_rows+c-1];
    }
            
}

/**
*  get a random item from a set of items with an accumulated probability array by binary search
*/
int AliSimulator::getRandomItemWithAccumulatedProbabilityMatrix(double *accumulated_probability_maxtrix, int starting_index, int num_columns)
{
    // generate a random number
    double random_number = random_double();
    
    return binarysearchItemWithAccumulatedProbabilityMatrix(accumulated_probability_maxtrix, random_number, starting_index, starting_index+(num_columns-1), starting_index)-starting_index;
}

/**
*  binary search an item from a set with accumulated probability array
*/
int AliSimulator::binarysearchItemWithAccumulatedProbabilityMatrix(double *accumulated_probability_maxtrix, double random_number, int start, int end, int first)
{
    // check search range
    if (start > end)
        return -1; // return -1 ~ not found
    
    // compute the center index
    int center = (start + end)/2;
    
    // if item is found at the center index -> return result
    if ((random_number <= accumulated_probability_maxtrix[center])
        && ((center == first)
            || (random_number > accumulated_probability_maxtrix[center - 1])))
        return center;
    
    // otherwise, search in the left/right side.
    if (random_number <= accumulated_probability_maxtrix[center])
        return binarysearchItemWithAccumulatedProbabilityMatrix(accumulated_probability_maxtrix, random_number, start, center - 1, first);
    else
        return binarysearchItemWithAccumulatedProbabilityMatrix(accumulated_probability_maxtrix, random_number, center + 1, end, first);
}

/**
*  write all sequences of a tree to an output file
*/
void AliSimulator::writeSequencesToFile(string file_path)
{
    try {
        ofstream out;
        out.exceptions(ios::failbit | ios::badbit);
        out.open(file_path.c_str());
        
        // write the first line <#taxa> <length_of_sequence>
        out <<(tree->leafNum) <<" "<<params->alisim_sequence_length<< endl;
        
        // write senquences of leaf nodes to file
        writeASequenceToFile(tree->aln, out, tree->root, tree->root);
        
        // close the file
        out.close();
    } catch (ios::failure) {
        outError(ERR_WRITE_OUTPUT, file_path);
    }
}

/**
*  write a sequence of a node to an output file
*/
void AliSimulator::writeASequenceToFile(Alignment *aln, ofstream &out, Node *node, Node *dad)
{
    if (node->isLeaf() && node->name!=ROOT_NAME) {
        out <<node->name <<" "<<convertEncodedSequenceToReadableSequence(aln, node->sequence) << endl;
    }
    
    NeighborVec::iterator it;
    FOR_NEIGHBOR(node, dad, it) {
        writeASequenceToFile(aln, out, (*it)->node, node);
    }
}

/**
*  convert an encoded sequence (with integer numbers) to a readable sequence (with ACGT...)
*/
string AliSimulator::convertEncodedSequenceToReadableSequence(Alignment *aln, IntVector sequence)
{
    string output_sequence = "";

    for (int state : sequence)
        output_sequence = output_sequence + aln->convertStateBackStr(state);
        
    return output_sequence;
    
};
