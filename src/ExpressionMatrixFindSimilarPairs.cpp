#include "ExpressionMatrix.hpp"
#include "ExpressionMatrixSubset.hpp"
#include "SimilarPairs.hpp"
#include "timestamp.hpp"
using namespace ChanZuckerberg;
using namespace ExpressionMatrix2;

#include <chrono>
#include "fstream.hpp"



// Find similar cell pairs by looping over all pairs,
// taking into account only genes in the specified gene set.
// This is O(N**2) slow because it loops over cell pairs.
void ExpressionMatrix::findSimilarPairs0(
    ostream& out,
    const string& geneSetName,      // The name of the gene set to be used.
    const string& cellSetName,      // The name of the cell set to be used.
    const string& similarPairsName, // The name of the SimilarPairs object to be created.
    size_t k,                       // The maximum number of similar pairs to be stored for each cell.
    double similarityThreshold
    )
{
    // Sanity check.
    CZI_ASSERT(similarityThreshold <= 1.);

    // Locate the gene set and verify that it is not empty.
    const auto itGeneSet = geneSets.find(geneSetName);
    if(itGeneSet == geneSets.end()) {
        throw runtime_error("Gene set " + geneSetName + " does not exist.");
    }
    const GeneSet& geneSet = itGeneSet->second;
    if(geneSet.size() == 0) {
        throw runtime_error("Gene set " + geneSetName + " is empty.");
    }

    // Locate the cell set and verify that it is not empty.
    const auto& it = cellSets.cellSets.find(cellSetName);
    if(it == cellSets.cellSets.end()) {
        throw runtime_error("Cell set " + cellSetName + " does not exist.");
    }
    const MemoryMapped::Vector<CellId>& cellSet = *(it->second);
    if(cellSet.size() == 0) {
        throw runtime_error("Cell set " + cellSetName + " is empty.");
    }

    // Create the SimilarPairs object where we will store the pairs.
    SimilarPairs similarPairs(directoryName, similarPairsName, geneSetName, cellSetName, k);

    // Create the expression matrix subset for this gene set and cell set.
    const string expressionMatrixSubsetName = directoryName + "/tmp-ExpressionMatrixSubset-" + similarPairsName;
    ExpressionMatrixSubset expressionMatrixSubset(
        expressionMatrixSubsetName, geneSet, cellSet, cellExpressionCounts);


    // Loop over all pairs.
    out << timestamp << "Begin computing similarities for all cell pairs." << endl;
    const auto t0 = std::chrono::steady_clock::now();
    for(CellId localCellId0=0; localCellId0!=similarPairs.cellCount()-1; localCellId0++) {
        if(localCellId0>0 && ((localCellId0%100) == 0)) {
            out << timestamp << "Working on cell " << localCellId0 << " of " << cellSet.size() << endl;
        }

        // Find all cells with similarity better than the specified threshold.
        for(CellId localCellId1=localCellId0+1; localCellId1!=similarPairs.cellCount(); localCellId1++) {
            const double similarity = expressionMatrixSubset.computeCellSimilarity(localCellId0, localCellId1);

            // If the similarity is sufficient, pass it to the SimilarPairs container,
            // which will make the decision whether to store it, depending on the
            // number of pairs already stored for cellId0 and cellId1.
            if(similarity > similarityThreshold) {
                similarPairs.add(localCellId0, localCellId1, similarity);
            }
        }
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double t01 = 1.e-9 * double((std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0)).count());


    // Sort the similar pairs for each cell by decreasing similarity.
    similarPairs.sort();


    out << "Time for all pairs: " << t01 << " s." << endl;
    const CellId cellCount = CellId(cellSet.size());
    out << "Time per pair: " << t01/(0.5*double(cellCount)*double(cellCount-1)) << " s." << endl;
}

void ExpressionMatrix::findSimilarPairs0(
    const string& geneSetName,      // The name of the gene set to be used.
    const string& cellSetName,      // The name of the cell set to be used.
    const string& similarPairsName, // The name of the SimilarPairs object to be created.
    size_t k,                       // The maximum number of similar pairs to be stored for each cell.
    double similarityThreshold
    )
{
    findSimilarPairs0(cout, geneSetName, cellSetName, similarPairsName, k, similarityThreshold);
}



// Dump to csv file a set of similar cell pairs.
void ExpressionMatrix::writeSimilarPairs(const string& name) const
{
    SimilarPairs similarPairs(directoryName, name, true);
    ofstream csvOut("SimilarPairs-" + name + ".csv");
    csvOut << "Cell0,Cell1,Computed,Exact AllGenes\n";

    for(CellId cellId0=0; cellId0<cellCount(); cellId0++) {
        for(size_t i=0; i<similarPairs.size(cellId0); i++) {
            const auto& p = similarPairs.begin(cellId0)[i];
            csvOut << cellId0 << ",";
            csvOut << p.first << ",";
            csvOut << p.second << ",";
            csvOut << computeCellSimilarity(cellId0, p.first) << "\n";
        }
    }

}



// Remove a similar pairs object given its name.
// This throws an exception if the requested SimilarPairs object does not exist.
void ExpressionMatrix::removeSimilarPairs(const string& name)
{
    try {
        SimilarPairs similarPairs(directoryName, name, false);
        similarPairs.remove();
    } catch(runtime_error e) {
        cout << e.what() << endl;
        throw runtime_error("Error removing similar pairs object " + name);
    }
}



// Unit test for class ExpressionMatrixSubset.
void ExpressionMatrix::testExpressionMatrixSubset(CellId cellId0, CellId cellId1) const
{
    cout << "Test ExpressionMatrixSubset on cell ids " << cellId0 << " " << cellId1 << endl;

    // Locate the gene set.
    const auto itGeneSet = geneSets.find("AllGenes");
    CZI_ASSERT(itGeneSet != geneSets.end());
    const GeneSet& geneSet = itGeneSet->second;

    // Locate the cell set.
    const auto itCellSet = cellSets.cellSets.find("AllCells");
    CZI_ASSERT(itCellSet != cellSets.cellSets.end());
    const CellSet& cellSet = *(itCellSet->second);

    const string expressionMatrixSubsetName = directoryName + "/tmp-ExpressionMatrixSubset-Test";
    ExpressionMatrixSubset expressionMatrixSubset(
        expressionMatrixSubsetName, geneSet, cellSet, cellExpressionCounts);

    cout << "Exact " << computeCellSimilarity(cellId0, cellId1) << endl << endl;
    cout << "Subset " << expressionMatrixSubset.computeCellSimilarity(cellId0, cellId1) << endl;
}
