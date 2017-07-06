// CZI.
#include "CellSimilarityGraph.hpp"
#include "color.hpp"
#include "CZI_ASSERT.hpp"
#include "deduplicate.hpp"
#include "iostream.hpp"
#include "iterator.hpp"
#include "SimilarPairs.hpp"
#include "timestamp.hpp"
using namespace ChanZuckerberg::ExpressionMatrix2;

// Boost libraries.
#include "boost_lexical_cast.hpp"
#include <boost/graph/graphviz.hpp>
#include <boost/graph/iteration_macros.hpp>
#include <boost/algorithm/string.hpp>
using boost::algorithm::split;
using boost::algorithm::is_any_of;

// Standard libraries.
#include "algorithm.hpp"
#include "fstream.hpp"
#include "set.hpp"
#include "stdexcept.hpp"
#include "utility.hpp"
#include "vector.hpp"
#include <limits>



CellSimilarityGraph::CellSimilarityGraph(
    const MemoryMapped::Vector<CellId>& cellSet, // The cell set to be used.
    const string& similarPairsName,              // The name of the SimilarPairs object to be used to create the graph.
    double similarityThreshold,                  // The minimum similarity to create an edge.
    size_t maxConnectivity                      // The maximum number of neighbors (k of the k-NN graph).
 )
{
    typedef SimilarPairs::Pair Pair;

    // Create the SimilarPairs object.
    const SimilarPairs similarPairs(similarPairsName);

    // Create a vertex for each cell in the cell set.
    for(const CellId cellId: cellSet) {
        const vertex_descriptor v = boost::add_vertex(CellSimilarityGraphVertex(cellId), graph());
        vertexTable.insert(make_pair(cellId, v));
    }



    // Create the edges.
    // The similar pairs are sorted by decreasing similarity.
    vector< pair<vertex_descriptor, float > > pairs;
    for(const CellId cellId0: cellSet) {
        const vertex_descriptor v0 = vertexTable[cellId0];

        // Find the best up to k pairs such that the other vertex
        // is also in the cell set.
        pairs.clear();
        const Pair* begin = similarPairs.begin(cellId0);
        const Pair* end = similarPairs.end(cellId0);
        for(const Pair* p=begin; p!=end; ++p) {
            const float similarity = p->second;
            if(similarity < similarityThreshold) {
                break;
            }
            const CellId cellId1 = p->first;
            const auto it1 = vertexTable.find(cellId1);
            if(it1 == vertexTable.end()) {
                continue;
            }
            pairs.push_back(make_pair(it1->second, similarity));
            if(pairs.size() == maxConnectivity) {
                break;
            }
        }

        // Add an edge for each pair we found.
        // Some of the edges may already exist (we added them when
        // processing cellId1).
        for(const pair<vertex_descriptor, float >& p: pairs) {
            const vertex_descriptor v1 = p.first;
            edge_descriptor e;
            bool edgeExists = false;
            tie(e, edgeExists) = boost::edge(v0, v1, graph());
            if(edgeExists) {
                continue;
            }
            boost::add_edge(v0, v1, CellSimilarityGraphEdge(p.second), graph());
        }
    }

    // cout << "The cell similarity graph has " << boost::num_vertices(graph());
    // cout << " vertices and " << num_edges(graph()) << " edges." << endl;

}



// Write the graph in Graphviz format.
void CellSimilarityGraph::write(const string& fileName) const
{
	ofstream outputFileStream(fileName);
	if(!outputFileStream) {
		throw runtime_error("Error opening " + fileName);
	}
	write(outputFileStream);
}
void CellSimilarityGraph::write(ostream& s) const
{
	Writer writer(*this);
	boost::write_graphviz(s, graph(), writer, writer, writer,
	    boost::get(&CellSimilarityGraphVertex::cellId, graph()));
}

CellSimilarityGraph::Writer::Writer(const Graph& graph) :
    graph(graph)
{
}



void CellSimilarityGraph::Writer::operator()(std::ostream& s) const
{
    s << "tooltip=\"\";";
    s << "node [shape=point];";
}


// Write out a vertex of the cell similarity graph.
void CellSimilarityGraph::Writer::operator()(std::ostream& s, vertex_descriptor v) const
{
    // Get the vertex.
	const CellSimilarityGraphVertex& vertex = graph[v];

	// Begin vertex attributes.
	s << "[";

	// Add a tooltip that shows the cell id.
	s << "tooltip=" << vertex.cellId;

	// End vertex attributes.
	s << "]";
}



void CellSimilarityGraph::Writer::operator()(std::ostream& s, edge_descriptor e) const
{
    // Get the edge.
    const CellSimilarityGraphEdge& edge = graph[e];

    // Begin edge attributes.
    s << "[";

    // Add a tooltip that shows the similarity.
    s.precision(2);
    s.setf(std::ios::fixed);
    s << "tooltip=\"" << edge.similarity << "\"";

    // End edge attributes.
    s << "]";
}



#if 0

// Only keep an edge if it is one of the best n edges for either
// of the two vertices.
void CellSimilarityGraph::keepBestEdgesOnly(std::size_t n)
{
	// Loop over all vertices.
	set<edge_descriptor> edgesToBeKept;
	BGL_FORALL_VERTICES(v, graph(), Graph) {

		// Find the edges of this vertex, each with their similarity.
		vector<pair<double, edge_descriptor>> vEdges;
		BGL_FORALL_OUTEDGES(v, e, graph(), Graph) {
			vEdges.push_back(make_pair(graph()[e].similarity, e));
		}

		// Only keep the best n.
		if(vEdges.size()>n) {
			sort(vEdges.begin(), vEdges.end(), std::greater<pair<double, edge_descriptor>>());
			vEdges.resize(n);
		}

		// Add the edges to be kept to the set.
		for(const auto& p: vEdges) {
			edgesToBeKept.insert(p.second);
		}
	}

	// Find the edges to be removed.
	vector<edge_descriptor> edgesToBeRemoved;
	BGL_FORALL_EDGES(e, graph(), Graph) {
		if(edgesToBeKept.find(e) == edgesToBeKept.end()) {
			edgesToBeRemoved.push_back(e);
		}
	}

	// Remove them.
	for(const edge_descriptor e: edgesToBeRemoved) {
		boost::remove_edge(e, graph());
	}
}
#endif


// Remove isolated vertices and returns\ the number of vertices that were removed
size_t CellSimilarityGraph::removeIsolatedVertices()
{
    vector<vertex_descriptor> verticesToBeRemoved;
    BGL_FORALL_VERTICES(v, graph(), Graph) {
        if(out_degree(v, graph()) == 0) {
            verticesToBeRemoved.push_back(v);
        }
    }

    for(const vertex_descriptor v: verticesToBeRemoved) {
        const CellSimilarityGraphVertex& vertex = graph()[v];
        vertexTable[vertex.cellId] = null_vertex();
        boost::remove_vertex(v, graph());
    }

    return verticesToBeRemoved.size();
}




// Use Graphviz to compute the graph layout and store it in the vertex positions.
void CellSimilarityGraph::computeLayout()
{
    // Write the graph in Graphviz format.
    write("Graph.dot");

    // Run sfdp with output in Graphviz plain format.
    // See http://www.graphviz.org/content/output-formats#dplain
    const int returnCode = ::system("sfdp -O -Tplain -Goverlap=true -Gsmoothing=triangle Graph.dot");
    if(returnCode == -1) {
        throw runtime_error("Error running sfdp.");
    }



    // Extract vertex positions from the output.
    ifstream file("Graph.dot.plain");
    string line;
    vector<string> tokens;
    while(true) {

        // Get a line.
        getline(file, line);
        if(!file) {
            break;
        }

        // Parse it.
        split(tokens, line, is_any_of(" "));

        // Only parse lines that describe vertices.
        CZI_ASSERT(tokens.size() >= 1);
        if(tokens[0] != "node") {
            continue;
        }
        CZI_ASSERT(tokens.size() >= 4);

        // Extract the positions for this vertex.
        try {
            const CellId cellId = lexical_cast<CellId>(tokens[1]);
            const vertex_descriptor v = vertexTable[cellId];
            CZI_ASSERT(v != null_vertex());
            CellSimilarityGraphVertex& vertex = graph()[v];
            vertex.position[0] = lexical_cast<double>(tokens[2]);
            vertex.position[1] = lexical_cast<double>(tokens[3]);
        } catch(std::exception& e) {
            cout << "Error processing the following line of Graph.dot.plain:" << endl;
            cout << line << endl;
            throw;
        }
    }
}




// Compute minimum and maximum coordinates of all the vertices.
void CellSimilarityGraph::computeCoordinateRange(
    double& xMin,
    double& xMax,
    double& yMin,
    double& yMax) const
{
    xMin = std::numeric_limits<double>::max();
    xMax = std::numeric_limits<double>::min();
    yMin = std::numeric_limits<double>::max();
    yMax = std::numeric_limits<double>::min();
    BGL_FORALL_VERTICES(v, graph(), Graph) {
        const CellSimilarityGraphVertex& vertex = graph()[v];
        const double x = vertex.position[0];
        const double y = vertex.position[1];
        xMin = min(xMin, x);
        xMax = max(xMax, x);
        yMin = min(yMin, y);
        yMax = max(yMax, y);
    }

}



// Write the graph in svg format.
// This does not use Graphviz. It uses the graph layout stored in the vertices,
// and previously computed using Graphviz.
// The vertex coordinates are used without any transformation.
// The last argument specifies the color assigned to each vertex group.
// If empty, vertex groups are not used, and each vertex is drawn
// with its own color.
void CellSimilarityGraph::writeSvg(
    ostream& s,
	bool hideEdges,
    double svgSizePixels,
    double xViewBoxCenter,
    double yViewBoxCenter,
    double viewBoxHalfSize,
    double vertexRadius,
    double edgeThickness,
    const map<int, string>& groupColors
    ) const
{




    // Start the svg object.
    s <<
        "<p><svg id=graphSvg width='" << svgSizePixels << "' height='" << svgSizePixels <<
        "' viewBox='" <<
        xViewBoxCenter-viewBoxHalfSize << " " << yViewBoxCenter-viewBoxHalfSize << " " <<
        2.*viewBoxHalfSize << " " << 2.*viewBoxHalfSize <<
        "'>";



    // Draw the edges first.
    // This makes it easier to see the vertices and their tooltips.
    if(!hideEdges) {
		s << "<g id=edges>";
		BGL_FORALL_EDGES(e, graph(), Graph) {
			const vertex_descriptor v1 = source(e, graph());
			const vertex_descriptor v2 = target(e, graph());

			const CellSimilarityGraphVertex& vertex1 = graph()[v1];
			const CellSimilarityGraphVertex& vertex2 = graph()[v2];

			const double x1 = vertex1.position[0];
			const double y1 = vertex1.position[1];
			const double x2 = vertex2.position[0];
			const double y2 = vertex2.position[1];

			s << "<line x1='" << x1 << "' y1='" << y1 << "'";
			s << " x2='" << x2 << "' y2='" << y2 << "'";

			s << " style='stroke:";
			const string& color = graph()[e].color;
			if(color.empty()) {
				s << "black";
			} else {
				s << color;
			}
			s << ";stroke-width:" << edgeThickness << "' />";
		}
		s << "</g>";
    }



    // If the groupColors map is empty, vertex groups are not used,
    // and each vertex is drawn in its own color.
    // We still write two levels of groups, because the javascript code
    // to change vertex size expects that structure.
    if(groupColors.empty()) {
        s << "<g id=vertices><g>";
        BGL_FORALL_VERTICES(v, graph(), Graph) {
            const CellSimilarityGraphVertex& vertex = graph()[v];
            const double x = vertex.position[0];
            const double y = vertex.position[1];
            s <<
                "<a xlink:href='cell?cellId=" << vertex.cellId << "'>"
                "<circle cx='" << x << "' cy='" << y << "' r='" << vertexRadius << "' stroke=none";
            if(!vertex.color.empty()) {
                s << " fill='" << vertex.color << "'";
            }
            s <<
                ">"
                "<title>Cell " << vertex.cellId << "</title></circle>"
                "</a>"
                ;
        }
        s << "</g></g>";
    }



    // Otherwise, colors are specified for each group, not for each vertex.
    else {


        // Find the vertices in each group.
        vector< vector<vertex_descriptor> > groups;
        BGL_FORALL_VERTICES(v, graph(), Graph) {
            const CellSimilarityGraphVertex& vertex = graph()[v];
            const size_t group = vertex.group;
            if(groups.size() <= group) {
                groups.resize(group+1);
            }
            groups[group].push_back(v);
        }

        // A circle at the center.
        // s << "<circle cx='" << svgSizePixels/2 << "' cy='" << svgSizePixels/2 << "' r='" << 10 << "' stroke='black' stroke-width='3' fill='red' />";

        // Draw the vertices, one group at a time.
        s << "<g id=vertices>";

        // Loop over all groups.
        for(int iGroup=0; iGroup<int(groups.size()); iGroup++) {
            string groupColor;
            const auto it = groupColors.find(iGroup);
            if(it == groupColors.end()) {
                groupColor = "black";
            } else {
                groupColor = it->second;
            }
            auto& group= groups[iGroup];
            s << "<g id=vertexGroup" << iGroup << " style='fill:" << groupColor << "'>";

            // Loop over all vertices of this group.
            for(const vertex_descriptor v: group) {
                const CellSimilarityGraphVertex& vertex = graph()[v];

                const double x = vertex.position[0];
                const double y = vertex.position[1];
                s <<
                    "<a xlink:href='cell?cellId=" << vertex.cellId << "'>"
                    "<circle cx='" << x << "' cy='" << y << "' r='" << vertexRadius << "' stroke=none>"
                    "<title>Cell " << vertex.cellId << "</title></circle>"
                    "</a>"
                    ;
            }
            s << "</g>";
        }
        s << "</g>";
    }

}



// Clustering using the label propagation algorithm.
// The cluster each vertex is assigned to is stored in the clusterId data member of the vertex.
void CellSimilarityGraph::labelPropagationClustering(
    ostream& out,
    size_t seed,                            // Seed for random number generator.
    size_t stableIterationCountThreshold,   // Stop after this many iterations without changes.
    size_t maxIterationCount                // Stop after this many iterations no matter what.
    )
{
    out << timestamp << "Clustering by label propagation begins." << endl;
    out << "Seed for random number generator is " << seed << "." << endl;
    out << "Will stop after " << stableIterationCountThreshold << " iterations without changes." << endl;
    out << "Maximum number of iterations is " << maxIterationCount << "." << endl;

    // Set the cluster of each vertex equal to its cell id.
    BGL_FORALL_VERTICES(v, graph(), CellSimilarityGraph) {
        CellSimilarityGraphVertex& vertex = graph()[v];
        vertex.clusterId = vertex.cellId;
    }

    // Create the random number generator using the specified seed.
    std::mt19937 randomGenerator(seed);

    // Vector with all the vertices in the graph, in the same order as in the vertex table.
    vector<vertex_descriptor> allVertices;
    for(const auto& p: vertexTable) {
    	const vertex_descriptor v = p.second;
    	if(v != null_vertex()) {
    		allVertices.push_back(p.second);
    	}
    }


    // Vector to contain the vertices in the random order to be used at each iteration.
    vector<vertex_descriptor> shuffledVertices;

    // Counter of the number of stable iterations
    // (iterations without changes).
    size_t stableIterationCount = 0;



    // Iterate.
    for(size_t iteration=0; iteration<maxIterationCount; iteration++) {
        size_t changeCount = 0;

        // Create a random shuffle of the vertices, to be used for this iteration.
        shuffledVertices = allVertices;
        std::shuffle(shuffledVertices.begin(), shuffledVertices.end(), randomGenerator);

        // Process the vertices in the order determined by the random shuffle.
        for(const vertex_descriptor v0: shuffledVertices) {
            CZI_ASSERT(v0 != CellSimilarityGraph::null_vertex());
            CellSimilarityGraphVertex& vertex0 = graph()[v0];
            const size_t clusterId0 = vertex0.clusterId;

            // Map to contain the clusters of the neighbors of this vertex, each with its total weight.
            // This could be written for better performance.
            map<uint32_t, double> clusterMap;

            // Loop over the edges of this vertex.
            BGL_FORALL_OUTEDGES(v0, e, graph(), CellSimilarityGraph) {
                const vertex_descriptor v1 = target(e, graph());
                const double weight = graph()[e].similarity;
                const uint32_t clusterId1 = graph()[v1].clusterId;

                const auto it = clusterMap.find(clusterId1);
                if(it == clusterMap.end()) {
                    clusterMap.insert(make_pair(clusterId1, weight));
                } else {
                    it->second += weight;
                }
            }
            if(clusterMap.empty()) {
                continue;   // There were no neighbors. Nothing to do.
            }

            // Find the cluster with the most weight.
            // Note that we don't bother to deal with ties explicitly.
            uint32_t bestClusterId = std::numeric_limits<uint32_t>::max();
            double bestWeight = -1;
            for(const auto& p: clusterMap) {
                const uint32_t clusterId = p.first;
                const double weight = p.second;
                if(weight > bestWeight) {
                    bestClusterId = clusterId;
                    bestWeight = weight;
                }
            }
            CZI_ASSERT(bestClusterId != std::numeric_limits<uint32_t>::max());

            // The best cluster id becomes the cluster id of vertex v0.
            if(bestClusterId != clusterId0) {
                vertex0.clusterId = bestClusterId;
                ++changeCount;
            }

        }
        out << "Iteration " << iteration << ": " << changeCount << " changes." << endl;

        // Update the number of stable iterations (iterations without changes).
        if(changeCount) {
            stableIterationCount = 0;
        } else {
            ++stableIterationCount;
        }

        // If we have done enough stable iterations, stop.
        if(stableIterationCount == stableIterationCountThreshold) {
            break;
        }
    }


    if(stableIterationCount == stableIterationCountThreshold) {
        out << "Terminating because the specified number stable iterations was achieved." << endl;
    } else {
        out << "Terminating because the maximum number of iterations was reached." << endl;
    }



    // Compute the size of each cluster.
    map<uint32_t, size_t> clusterSize;    // Key=clusterId, Value=cluster size
    BGL_FORALL_VERTICES(v, graph(), CellSimilarityGraph) {
        const uint32_t clusterId = graph()[v].clusterId;
        const auto it = clusterSize.find(clusterId);
        if(it == clusterSize.end()) {
            clusterSize.insert(make_pair(clusterId, 1));
        } else {
            ++(it->second);
        }
    }



    // Renumber the clusters beginning at 0 and in order of decreasing cluster size.
    vector< pair<size_t, uint32_t> > clusterSizeVector;   // first:cluster size, second: clusterId
    for(const auto& p: clusterSize) {
        clusterSizeVector.push_back(make_pair(p.second, p.first));
    }
    sort(clusterSizeVector.begin(), clusterSizeVector.end(), std::greater< pair<size_t, size_t> >());
    out << "Cluster sizes:";
    for(size_t newClusterId=0; newClusterId<clusterSizeVector.size(); newClusterId++) {
        const auto& p = clusterSizeVector[newClusterId];
        out << " " << p.first;
    }
    out << endl;
    map<uint32_t, uint32_t> clusterMap; // Key: old clusterId. Value: new clustyerId.
    for(uint32_t newClusterId=0; newClusterId<clusterSizeVector.size(); newClusterId++) {
        const uint32_t oldClusterId = clusterSizeVector[newClusterId].second;
        clusterMap.insert(make_pair(oldClusterId, newClusterId));
    }

    // Update the vertices to reflect the new cluster numbering.
    BGL_FORALL_VERTICES(v, graph(), CellSimilarityGraph) {
        CellSimilarityGraphVertex& vertex = graph()[v];
        vertex.clusterId = clusterMap[vertex.clusterId];
    }



    out << timestamp << "Clustering by label propagation ends." << endl;
}



// Assign integer colors to groups.
// The same color can be used for multiple groups, but if two
// groups are joined by one or more edges they must have distinct colors.
// On return, colorTable[group] contains the integer color assigned to each group.
// This processes the groups in increasing order beginning at group 0,
// so it is best if the group numbers are all contiguous, starting at zero,
// and in decreasing size of group.
void CellSimilarityGraph::assignColorsToGroups(vector<uint32_t>& colorTable)
{
    // Start with no colors assigned.
    colorTable.clear();

    // Find the vertices of each group.
    vector< vector<vertex_descriptor> > groupVertices;
    BGL_FORALL_VERTICES(v, graph(), CellSimilarityGraph) {
        const uint32_t groupId = graph()[v].group;
        if(groupVertices.size() <= groupId) {
            groupVertices.resize(groupId + 1);
        }
        groupVertices[groupId].push_back(v);
    }
    const size_t groupCount = groupVertices.size();


    // Create the group graph.
    // Each vertex corresponds ot a group.
    typedef boost::adjacency_list<boost::setS, boost::vecS, boost::undirectedS> GroupGraph;
    GroupGraph groupGraph(groupCount);
    BGL_FORALL_EDGES(e, graph(), CellSimilarityGraph) {
        const vertex_descriptor v0 = source(e, graph());
        const vertex_descriptor v1 = target(e, graph());
        const CellSimilarityGraphVertex& vertex0 = graph()[v0];
        const CellSimilarityGraphVertex& vertex1 = graph()[v1];
        const uint32_t group0 = vertex0.group;
        const uint32_t group1 = vertex1.group;
        if(group0 != group1) {
            boost::add_edge(group0, group1, groupGraph);
        }
    }

    // ofstream graphOut("GroupGraph.dot");
    // boost::write_graphviz(graphOut, groupGraph);

    // For each group, we have to look at edges to lowered numbered groups
    vector<uint32_t> adjacentColors;
    for(uint32_t group0=0; group0<groupCount; group0++) {
        adjacentColors.clear();
        // cout << "Already colored groups adjacent to group " << group0 << ":";
        BGL_FORALL_OUTEDGES(group0, e, groupGraph, GroupGraph) {
            const uint32_t group1 = uint32_t(target(e, groupGraph));
            if(group1 < group0) {
                // cout << " " << group1;
                adjacentColors.push_back(colorTable[group1]);
            }
        }
        // cout << endl;
        deduplicate(adjacentColors);
        // cout << "Already assigned colors adjacent to group " << group0 << ": ";
        // copy(adjacentColors.begin(), adjacentColors.end(), ostream_iterator<uint32_t>(cout, " "));
        // cout << endl;

        // Assign to this group the smallest integer color that does not appear
        // in the adjacent groups.
        for(uint32_t color=0; color<adjacentColors.size(); color++) {
            if(adjacentColors[color] != color) {
                colorTable.push_back(color);
                break;
            }
        }
        if(colorTable.size() == group0) {
            colorTable.push_back(uint32_t(adjacentColors.size()));
        }

        // cout << "Group " << group0 << " assigned color " << colorTable[group0] << endl;
    }
}
