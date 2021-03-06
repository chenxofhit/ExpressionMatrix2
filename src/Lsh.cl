// This file defines OpenCL kernel code
// used by class Lsh (see Lsh.hpp and LshGpu.cpp).




const string code = R"###(



// Kernel 0: compute the number of mismatches between the signature
// of cell cellId0 and the signatures of all other cells.
// This is parallelized over cellId1.
// This is a simple but working kernel.
// It has high overhead and low performance
// because of the small amount of work done by each instance.
void kernel kernel0(
    uint cellCount,
    ulong lshWordCount,
    global const ulong* signatures, 
    uint cellId0, 
    global ushort* mismatchCounts)
{ 
    uint cellId1 = get_global_id(0);
    global const ulong* signature0 = signatures + cellId0 * lshWordCount;
    global const ulong* signature1 = signatures + cellId1 * lshWordCount;
    ulong mismatchCount = 0;
    uint k;
    for(k=0; k<lshWordCount; k++) {
        mismatchCount += popcount((*signature0++) ^ (*signature1++));
    }
    mismatchCounts[cellId1] = mismatchCount;
}



// Kernel 1: compute the number of mismatches between the signature
// of cells in range [cellId0Begin, cellId0End]
// and the signatures of all other cells.
// This is parallelized over cellId1.
// This has lower overhead than kernel 0
// because each kernel instance does more work
// (processes n0 values of cellId0 instead of just one).
// For efficient memory access, the blockSize mismatch counts 
// for each cellId1 are stored contiguously.
void kernel kernel1(
    uint cellCount,
    ulong lshWordCount,
    global const ulong* signatures, 
    uint cellId0Begin, 
    uint cellId0End,
    uint blockSize,
    global ushort* mismatchCounts)
{ 
    uint cellId1 = get_global_id(0);
    global const ulong* signature0Begin = signatures + cellId0Begin * lshWordCount;
    global const ulong* signature1Begin = signatures + cellId1 * lshWordCount;
    global ushort* mismatchCount1 = mismatchCounts + blockSize*cellId1;
    
    // Loop over the values of cellId0 assigned to this instance of the kernel.
    uint cellId0;
    for(cellId0=cellId0Begin; cellId0!=cellId0End; ++cellId0, signature0Begin+=lshWordCount) {
    
        // Compute the number of mismatches between the signatures
        // of cellId0 and cellId1.
        global const ulong* signature0 = signature0Begin;
        global const ulong* signature1 = signature1Begin;
        ulong mismatchCount = 0;
        uint k;
        for(k=0; k<lshWordCount; k++) {
            mismatchCount += popcount((*signature0++) ^ (*signature1++));
        }
        *mismatchCount1++ = mismatchCount;
    }
}



// Kernel 2: compute the number of mismatches between the signature
// of cells cellId0 and the signatures of all other cells.
// This is parallelized over cellId0. That is, each instance of the kernel
// works on a single value of cellId0.
// The similar cells are stored by mismatchCount.
// For each cellId0, we have a neighbors buffer of size
// (mismatchCountThreshold+1)*k, with k entries for each value
// of the mismatchCount between 0 and mismatchCountThreshold (included).
// We also have a neighborsCount buffer of size mismatchCountThreshold+1
// which contains the number of neighbors for each mismatchCount.

// The total size of the neighbors buffer for all cells in a block being
// processed in parallel is blockSize*(mismatchCountThreshold+1)*k.
// Each entry stores a CellId and therefore is 4 bytes, so
// the size in bytes is blockSize*(mismatchCountThreshold+1)*k.
// For k=100 and mismatchCountThreshold=400, this is blockSize*0.16MB,
// that is 160 MB for blockSize=1000 or 500 MB for blockSize=4000,
// so it is not a problem. Note that the size of this buffer does not
// increase with the number of cells, like the size of the signatures
// buffer does.


void kernel kernel2(
    ulong cellCount,
    ulong lshWordCount,
    ulong cellId0Begin, 
    ulong k,
    ulong mismatchCountThreshold,
    global const ulong* signatures, 
    global uint* neighbors, 
    global uint* neighborsCount)
{
    // i0 runs from zero to the number of cellId0 values we are processing in parallel.
    const ulong i0 = get_global_id(0);
    
    // Get the cellId0 corresponding to this i0.
    const ulong cellId0 = cellId0Begin + i0;
    
    // Access its signature.
    global ulong const * signature0Begin = signatures + cellId0 * lshWordCount;
    
    // We often need the mismatchCountThreshold + 1.
    const ulong mismatchCountThresholdPlus1 = mismatchCountThreshold + 1;
    
        
    // Access the portion of the neighbors ands neighborsCount buffers
    // corresponding to cellId0.
    global uint* const neighbors0 = neighbors + i0 * k * mismatchCountThresholdPlus1;
    global uint* const neighborsCount0 = neighborsCount + i0 * mismatchCountThresholdPlus1;
    
    // Initialize neighborsCount0.
    for(ulong i=0; i<=mismatchCountThreshold; i++) {
        neighborsCount0[i] = 0;
    }
    
    // Loop over all cells.
    global ulong const * signature1 = signatures; 
    for(ulong cellId1=0; cellId1!=cellCount; cellId1++) {
    
        // Compute the number of mismatches.
        global const ulong* signature0 = signature0Begin;
        ulong mismatchCount = 0;
        for(ulong j=0; j!=lshWordCount; j++) {
            mismatchCount += popcount((*signature0++) ^ (*signature1++));
        }
        
        // Only do the rest if the number of mismatches is small.
        // Hopefully this will use masked execution on the GPU.
        if(mismatchCount <= mismatchCountThreshold) {
        
            // Find how many neighbors with this mismatch count we have.
            uint count = neighborsCount0[mismatchCount];
            
            // Only do the rest if we have less than k neighbors with this count.
            // Hopefully this will use masked execution on the GPU.
            if(count < k) {
                neighbors0[k*mismatchCount + count] = cellId1;
                count++;
                neighborsCount0[mismatchCount] = count;
            }
        }
        
    }
    
}



/*******************************************************************************

Kernel 3: for a block of values of cellId0,
compute the number of mismatches between the signature of cellId0
and the signatures of values of cellId1 given in an input buffer.
Each cellId0 has its own set of cellId1 values.

This is used by findSimilarPairs7Gpu.

This is parallelized over cellId0. That is, each instance of the kernel
works on a single value of cellId0.

The similar cells for each value of celllId0 are stored by mismatchCount.
For each cellId0, we have a neighbors buffer of size
(mismatchCountThreshold+1)*k, with k entries for each value
of the mismatchCount between 0 and mismatchCountThreshold (included).
We also have a neighborsCount buffer of size mismatchCountThreshold+1
which contains the number of neighbors for each mismatchCount.

The total size of the neighbors buffer for all cells in a block being
processed in parallel is blockSize*(mismatchCountThreshold+1)*k.
Each entry stores a CellId and therefore is 4 bytes, so
the size in bytes is blockSize*(mismatchCountThreshold+1)*k.
For k=100 and mismatchCountThreshold=400, this is blockSize*0.16MB,
that is 160 MB for blockSize=1000 or 500 MB for blockSize=4000,
so it is not a problem. Note that the size of this buffer does not
increase with the number of cells, like the size of the signatures
buffer does.

*******************************************************************************/


void kernel kernel3(

    // The number of 64-bit words in each cell LSH signature.
    ulong lshWordCount,  

    // The first cell in the block current being processed.       
    ulong cellId0Begin,  

    // The maximum number of neighbors to store for each mismatchCount value.  
    ulong k,  

    // The maximum mismatchCount for which neighbors should be stored.                 
    ulong mismatchCountThreshold,

    // The maximum number of cellId1 values that we allow for a given 
    // value of cellId0. 
    ulong maxCheck,

    // The signatures of all the cells.
    // The stride between successive signatures is lshWordCount.
    global ulong const* signatures, 

    // Vector containing the number of cellId1 values to be processed
    // for each cellId0 in the current block.
    global uint const* cellId1Counts,

    // Vector containing the values of cellId1 to be processed
    // for each cellId0 in the current block.
    // The values for each cellId0 are stored with stride maxCheck.
    global uint const* cellId1s,

    // The number of neighbors stored for each mismatch count
    // and for all cells in the current block (see comments above).
    // The stride between successive values of cellId0 is mismatchCountThreshold+1.
    global uint* neighborsCount,     

    // The neighbor vector for all cells in the current block (see comments above).
    // The stride between successive values of cellId0 is (mismatchCountThreshold+1)*k.
    // The stride between successive values of mismatchCount is k.
    global uint* neighbors      

    )
{
    // i0 runs from zero to the number of cellId0 values we are processing in parallel.
    const ulong i0 = get_global_id(0);
    
    // Get the cellId0 corresponding to this i0.
    const ulong cellId0 = cellId0Begin + i0;
    
    // Access its signature.
    global ulong const * signature0Begin = signatures + cellId0 * lshWordCount;
    
    // We often need the mismatchCountThreshold + 1.
    const ulong mismatchCountThresholdPlus1 = mismatchCountThreshold + 1;
            
    // Access the portion of the neighbors and neighborsCount vectors
    // corresponding to cellId0.
    global uint* neighbors0 = neighbors + i0 * k * mismatchCountThresholdPlus1;
    global uint* neighborsCount0 = neighborsCount + i0 * mismatchCountThresholdPlus1;
    
    // Find the number of values of cellId1 to process for this cellId0.
    const uint cellId1Count = cellId1Counts[i0];

    // Access the portion of the cellId1s vector corresponding to cellId0.
    cellId1s += i0 * maxCheck;

    // printf("%li %li %i\n", i0, cellId0, cellId1Count);

    // Initialize neighborsCount0.
    for(ulong i=0; i<=mismatchCountThreshold; i++) {
        neighborsCount0[i] = 0;
    }



    // Loop over all values of cellId1.
    for(uint i1=0; i1<cellId1Count; i1++) {
        const uint cellId1 = cellId1s[i1];

        // Locate the signatures for cellId0 and cellId1.
        global ulong const * signature0 = signature0Begin;
        global ulong const * signature1 = signatures + lshWordCount * cellId1;

        // Compute the number of mismatches.
        ulong mismatchCount = 0;
        for(ulong j=0; j!=lshWordCount; j++) {
            mismatchCount += popcount((*signature0++) ^ (*signature1++));
        }

        // Only do the rest if the number of mismatches is small.
        if(mismatchCount <= mismatchCountThreshold) {
        
            // Find how many neighbors with this mismatch count we have.
            uint count = neighborsCount0[mismatchCount];
            
            // Only do the rest if we have less than k neighbors with this count.
            if(count < k) {
                neighbors0[k*mismatchCount + count] = cellId1;
                count++;
                neighborsCount0[mismatchCount] = count;
            }
        }
    }
}

)###";
