//
//  Grid3Duisp.h
//  ttcr
//
//  Created by Bernard Giroux on 2014-04-21.
//  Copyright (c) 2014 Bernard Giroux. All rights reserved.
//

#ifndef ttcr_Grid3Duisp_h
#define ttcr_Grid3Duisp_h

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <vector>

#include "Grid3Dui.h"
#include "Interpolator.h"
#include "Node3Disp.h"
#include "utils.h"

template<typename T1, typename T2>
class Grid3Duisp : public Grid3Dui<T1,T2,Node3Disp<T1,T2>> {
public:
	Grid3Duisp(const std::vector<sxyz<T1>>& no,
			   const std::vector<tetrahedronElem<T2>>& tet,
			   const int ns, const size_t nt=1, const int verbose=0) :
    Grid3Dui<T1,T2,Node3Disp<T1,T2>>(no, tet, nt), nsecondary(ns)
	{
		buildGridNodes(no, ns, nt, verbose);
		this->buildGridNeighbors();
	}
	
	~Grid3Duisp() {
	}
	
	int setSlowness(const std::vector<T1>& s) {
        if ( this->nPrimary != s.size() ) {
            std::cerr << "Error: slowness vectors of incompatible size.";
            return 1;
        }
        for ( size_t n=0; n<this->nPrimary; ++n ) {
            this->nodes[n].setNodeSlowness( s[n] );
        }
        if ( nsecondary>0 ) interpSlownessSecondary();
        return 0;
    }
	
	int raytrace(const std::vector<sxyz<T1>>&,
				 const std::vector<T1>&,
				 const std::vector<sxyz<T1>>&,
				 std::vector<T1>&,
				 const size_t=0) const;
	
    int raytrace(const std::vector<sxyz<T1>>&,
                 const std::vector<T1>&,
                 const std::vector<const std::vector<sxyz<T1>>*>&,
                 std::vector<std::vector<T1>*>&,
                 const size_t=0) const;
	
	int raytrace(const std::vector<sxyz<T1>>&,
                 const std::vector<T1>& ,
                 const std::vector<sxyz<T1>>&,
                 std::vector<T1>&,
                 std::vector<std::vector<sxyz<T1>>>&,
				 const size_t=0) const;
    
    int raytrace(const std::vector<sxyz<T1>>&,
                 const std::vector<T1>&,
                 const std::vector<const std::vector<sxyz<T1>>*>&,
                 std::vector<std::vector<T1>*>&,
                 std::vector<std::vector<std::vector<sxyz<T1>>>*>&,
                 const size_t=0) const;
	
	int raytrace(const std::vector<sxyz<T1>>&,
                 const std::vector<T1>& ,
                 const std::vector<sxyz<T1>>&,
                 std::vector<T1>&,
                 std::vector<std::vector<sxyz<T1>>>&,
                 std::vector<std::vector<siv<T1>>>&,
				 const size_t=0) const;
    
	
private:
	T2 nsecondary;

	void buildGridNodes(const std::vector<sxyz<T1>>&,
                        const int, const size_t, const int);
	
    void interpSlownessSecondary();

	void initQueue(const std::vector<sxyz<T1>>& Tx,
				   const std::vector<T1>& t0,
				   std::priority_queue<Node3Disp<T1,T2>*,
				   std::vector<Node3Disp<T1,T2>*>,
				   CompareNodePtr<T1>>& queue,
				   std::vector<Node3Disp<T1,T2>>& txNodes,
				   std::vector<bool>& inQueue,
				   std::vector<bool>& frozen,
				   const size_t threadNo) const;
	
    void prepropagate(const Node3Disp<T1,T2>& node,
					  std::priority_queue<Node3Disp<T1,T2>*, std::vector<Node3Disp<T1,T2>*>,
					  CompareNodePtr<T1>>& queue,
					  std::vector<bool>& inQueue,
					  std::vector<bool>& frozen,
					  size_t threadNo) const;
	
	void propagate(std::priority_queue<Node3Disp<T1,T2>*,
				   std::vector<Node3Disp<T1,T2>*>,
				   CompareNodePtr<T1>>& queue,
				   std::vector<bool>& inQueue,
				   std::vector<bool>& frozen,
				   const size_t threadNo) const;
	
	T1 getTraveltime(const sxyz<T1>& Rx,
					 const std::vector<Node3Disp<T1,T2>>& nodes,
					 const size_t threadNo) const;

	T1 getTraveltime(const sxyz<T1>& Rx,
					 const std::vector<Node3Disp<T1,T2>>& nodes,
					 T2& nodeParentRx,
					 T2& cellParentRx,
					 const size_t threadNo) const;
};


template<typename T1, typename T2>
void Grid3Duisp<T1,T2>::buildGridNodes(const std::vector<sxyz<T1>>& no,
                                       const int nsecondary,
                                       const size_t nt, const int verbose) {
	
	// primary nodes
	for ( T2 n=0; n<no.size(); ++n ) {
		this->nodes[n].setXYZindex( no[n].x, no[n].y, no[n].z, n );
		this->nodes[n].setPrimary(5);
	}
	T2 nNodes = static_cast<T2>(this->nodes.size());
	
    size_t nFaceNodes = 0;
    for ( int n=1; n<=(nsecondary-1); ++n ) nFaceNodes += n;
    
	std::map<std::array<T2,2>,std::vector<T2>> lineMap;
	std::array<T2,2> lineKey;
	typename std::map<std::array<T2,2>,std::vector<T2>>::iterator lineIt;
    
    size_t estLineNo = (this->tetrahedra.size()+this->tetrahedra.size()/10) * 6/2;
    size_t estFaceNo = (this->tetrahedra.size()+this->tetrahedra.size()/10) * 4/2;
    this->nodes.reserve( nNodes + estLineNo*nsecondary + estFaceNo*nFaceNodes );
    
	
	T2 iNodes[4][3] = {
		{0,1,2},  // (relative) indices of nodes of 1st triangle
		{1,2,3},  // (relative) indices of nodes of 2nd triangle
		{0,2,3},  // (relative) indices of nodes of 3rd triangle
		{0,1,3}   // (relative) indices of nodes of 4th triangle
	};
	
	//
	//              1
	//            ,/|`\
	//          ,/  |  `\
	//        ,0    '.   `4
	//      ,/       1     `\
	//    ,/         |       `\
	//   0-----5-----'.--------3
	//    `\.         |      ,/
	//       `\.      |     3
	//          `2.   '. ,/
	//             `\. |/
	//                `2
	//
	//
	//  triangle 0:  0-1  1-2  2-0     (first occurence of segment underlined)
	//               ---  ---  ---
	//  triangle 1:  1-2  2-3  3-1
	//                    ---  ---
	//  triangle 2:  0-2  2-3  3-0
	//                         ---
	//  triangle 3:  0-1  1-3  3-0
    
	if ( verbose>1 && nsecondary > 0 ) {
		std::cout << '\n';
	}
	
	// edge nodes
	Node3Disp<T1,T2> tmpNode(nt);
	for ( T2 ntet=0; ntet<this->tetrahedra.size(); ++ntet ) {
		
		if ( verbose>1 && nsecondary > 0 ) {
			std::cout << "\r  Building edge nodes: " << (100*ntet)/this->tetrahedra.size() << "%";
			std::cout.flush();
		}
		
		// for each triangle
		for ( T2 ntri=0; ntri<4; ++ntri ) {
            
			// push owner for primary nodes
			this->nodes[ this->tetrahedra[ntet].i[ntri] ].pushOwner( ntet );
			
			if ( nsecondary > 0 ) {
				// start from ntri to avoid redundancy
				for ( size_t nl=ntri; nl<3; ++nl ) {
					
					lineKey = {this->tetrahedra[ntet].i[ iNodes[ntri][nl] ],
						this->tetrahedra[ntet].i[ iNodes[ntri][(nl+1)%3] ]};
                    std::sort(lineKey.begin(), lineKey.end());
                    
					lineIt = lineMap.find( lineKey );
					if ( lineIt == lineMap.end() ) {
						// not found, insert new pair
						lineMap[ lineKey ] = std::vector<T2>(nsecondary);
					} else {
						for ( size_t n=0; n<lineIt->second.size(); ++n ) {
							// setting owners
							this->nodes[ lineIt->second[n] ].pushOwner( ntet );
						}
						continue;
					}
					
					sxyz<T1> d = (no[lineKey[1]]-no[lineKey[0]])/static_cast<T1>(nsecondary+1);
					
					for ( size_t n2=0; n2<nsecondary; ++n2 ) {
						tmpNode.setXYZindex(no[lineKey[0]].x+(1+n2)*d.x,
											no[lineKey[0]].y+(1+n2)*d.y,
											no[lineKey[0]].z+(1+n2)*d.z,
											nNodes );
						lineMap[lineKey][n2] = nNodes++;
						this->nodes.push_back( tmpNode );
						this->nodes.back().pushOwner( ntet );
					}
				}
			}
		}
	}
    
	if ( verbose>1 && nsecondary > 0 ) {
		std::cout << "\r  Building edge nodes: 100%\n";
	}
    
	if ( nsecondary > 1 ) {
        
		std::map<std::array<T2,3>,std::vector<T2>> faceMap;
		std::array<T2,3> faceKey;
		typename std::map<std::array<T2,3>,std::vector<T2>>::iterator faceIt;
		
		int ncut = nsecondary - 1;
		
		for ( T2 ntet=0; ntet<this->tetrahedra.size(); ++ntet ) {
			
			if ( verbose>1 ) {
				std::cout << "\r  Building face nodes: " << (100*ntet)/this->tetrahedra.size() << "%";
				std::cout.flush();
			}
			
			// for each triangle
			for ( T2 ntri=0; ntri<4; ++ntri ) {
				
				faceKey = {this->tetrahedra[ntet].i[ iNodes[ntri][0] ],
					this->tetrahedra[ntet].i[ iNodes[ntri][1] ],
					this->tetrahedra[ntet].i[ iNodes[ntri][2] ]};
				std::sort(faceKey.begin(), faceKey.end());
				
				
				faceIt = faceMap.find( faceKey );
				if ( faceIt == faceMap.end() ) {
					// not found, insert new pair
					faceMap[ faceKey ] = std::vector<T2>(nFaceNodes);
				} else {
					for ( size_t n=0; n<faceIt->second.size(); ++n ) {
						// setting owners
						this->nodes[ faceIt->second[n] ].pushOwner( ntet );
					}
					continue;
				}
				
				sxyz<T1> d1 = (no[faceKey[1]]-no[faceKey[0]])/static_cast<T1>(nsecondary+1);
				sxyz<T1> d2 = (no[faceKey[1]]-no[faceKey[2]])/static_cast<T1>(nsecondary+1);
				
                size_t ifn = 0;
				for ( size_t n=0; n<ncut; ++n ) {
					
					sxyz<T1> pt1 = no[faceKey[0]]+static_cast<T1>(1+n)*d1;
					sxyz<T1> pt2 = no[faceKey[2]]+static_cast<T1>(1+n)*d2;
					
					size_t nseg = ncut+1-n;
					
					sxyz<T1> d = (pt2-pt1)/static_cast<T1>(nseg);
					
					for ( size_t n2=0; n2<nseg-1; ++n2 ) {
						tmpNode.setXYZindex(pt1.x+(1+n2)*d.x,
											pt1.y+(1+n2)*d.y,
											pt1.z+(1+n2)*d.z,
											nNodes );
						faceMap[faceKey][ifn++] = nNodes++;
						this->nodes.push_back( tmpNode );
						this->nodes.back().pushOwner( ntet );
					}
				}
			}
		}
	}
	if ( verbose>1 && nsecondary > 0 ) {
		std::cout << "\r  Building face nodes: 100%\n";
	}
	
	this->nodes.shrink_to_fit();
}


template<typename T1, typename T2>
void Grid3Duisp<T1,T2>::interpSlownessSecondary() {
	
	T2 nNodes = this->nPrimary;
	
	std::map<std::array<T2,2>,std::vector<T2>> lineMap;
	std::array<T2,2> lineKey;
	typename std::map<std::array<T2,2>,std::vector<T2>>::iterator lineIt;

    size_t nFaceNodes = 0;
    for ( int n=1; n<=(nsecondary-1); ++n ) nFaceNodes += n;

	for ( T2 ntet=0; ntet<this->tetrahedra.size(); ++ntet ) {
		
		// for each triangle
		for ( T2 ntri=0; ntri<4; ++ntri ) {
            
			// start from ntri to avoid redundancy
			for ( size_t nl=ntri; nl<3; ++nl ) {
					
				lineKey = {this->tetrahedra[ntet].i[ iNodes[ntri][nl] ],
					this->tetrahedra[ntet].i[ iNodes[ntri][(nl+1)%3] ]};
				std::sort(lineKey.begin(), lineKey.end());
                    
				lineIt = lineMap.find( lineKey );
				if ( lineIt == lineMap.end() ) {
						// not found, insert new pair
					lineMap[ lineKey ] = std::vector<T2>(nsecondary);
				} else {
					continue;
				}
					
				T1 slope = (this->nodes[lineKey[1]].getNodeSlowness() - this->nodes[lineKey[0]].getNodeSlowness())/
				this->nodes[lineKey[1]].getDistance(this->nodes[lineKey[0]]);
				
				for ( size_t n2=0; n2<nsecondary; ++n2 ) {
					T1 s = this->nodes[lineKey[0]].getNodeSlowness() + slope * this->nodes[nNodes].getDistance(this->nodes[lineKey[0]]);
					this->nodes[nNodes].setNodeSlowness( s );
					lineMap[lineKey][n2] = nNodes++;
				}
			}
		}
	}
	
	
	
	if ( nsecondary > 1 ) {
        
		std::map<std::array<T2,3>,std::vector<T2>> faceMap;
		std::array<T2,3> faceKey;
		typename std::map<std::array<T2,3>,std::vector<T2>>::iterator faceIt;
		
		int ncut = nsecondary - 1;
		
		for ( T2 ntet=0; ntet<this->tetrahedra.size(); ++ntet ) {
			
			// for each triangle
			for ( T2 ntri=0; ntri<4; ++ntri ) {
				
				faceKey = {this->tetrahedra[ntet].i[ iNodes[ntri][0] ],
					this->tetrahedra[ntet].i[ iNodes[ntri][1] ],
					this->tetrahedra[ntet].i[ iNodes[ntri][2] ]};
				std::sort(faceKey.begin(), faceKey.end());
				
				
				faceIt = faceMap.find( faceKey );
				if ( faceIt == faceMap.end() ) {
					// not found, insert new pair
					faceMap[ faceKey ] = std::vector<T2>(nFaceNodes);
				} else {
					continue;
				}
				
				std::vector<Node3Disp<T1,T2>*> inodes;
				inodes.push_back( &(this->nodes[faceKey[0]]) );
				inodes.push_back( &(this->nodes[faceKey[1]]) );
				inodes.push_back( &(this->nodes[faceKey[2]]) );
				
				size_t ifn = 0;
				for ( size_t n=0; n<ncut; ++n ) {
					size_t nseg = ncut+1-n;
					for ( size_t n2=0; n2<nseg-1; ++n2 ) {
				
						T1 s = Interpolator<T1>::inverseDistance(this->nodes[nNodes], inodes);
						this->nodes[nNodes].setNodeSlowness( s );
						
						faceMap[faceKey][ifn++] = nNodes++;

					}
				}
			}
		}
	}
}


template<typename T1, typename T2>
int Grid3Duisp<T1,T2>::raytrace(const std::vector<sxyz<T1>>& Tx,
							    const std::vector<T1>& t0,
                                const std::vector<sxyz<T1>>& Rx,
                                std::vector<T1>& traveltimes,
                                const size_t threadNo) const {
    
	//	std::cout << "   running in thread no " << threadNo << std::endl;
    if ( this->check_pts(Tx) == 1 ) return 1;
    if ( this->check_pts(Rx) == 1 ) return 1;
    
    for ( size_t n=0; n<this->nodes.size(); ++n ) {
        this->nodes[n].reinit( threadNo );
    }
    
    CompareNodePtr<T1> cmp(threadNo);
    std::priority_queue< Node3Disp<T1,T2>*, std::vector<Node3Disp<T1,T2>*>,
    CompareNodePtr<T1>> queue( cmp );
    
    std::vector<Node3Disp<T1,T2>> txNodes;
    std::vector<bool> inQueue( this->nodes.size(), false );
    std::vector<bool> frozen( this->nodes.size(), false );
    
    initQueue(Tx, t0, queue, txNodes, inQueue, frozen, threadNo);
    
    propagate(queue, inQueue, frozen, threadNo);
    
    if ( traveltimes.size() != Rx.size() ) {
        traveltimes.resize( Rx.size() );
    }
    
    for (size_t n=0; n<Rx.size(); ++n) {
        traveltimes[n] = this->getTraveltime(Rx[n], this->nodes, threadNo);
    }
    return 0;
}

template<typename T1, typename T2>
int Grid3Duisp<T1,T2>::raytrace(const std::vector<sxyz<T1>>& Tx,
								const std::vector<T1>& t0,
								const std::vector<const std::vector<sxyz<T1>>*>& Rx,
								std::vector<std::vector<T1>*>& traveltimes,
								const size_t threadNo) const {
    
    if ( this->check_pts(Tx) == 1 ) return 1;
    for ( size_t n=0; n<Rx.size(); ++n )
        if ( this->check_pts(*Rx[n]) == 1 ) return 1;
    
    for ( size_t n=0; n<this->nodes.size(); ++n ) {
        this->nodes[n].reinit( threadNo );
    }
    
    CompareNodePtr<T1> cmp(threadNo);
    std::priority_queue< Node3Disp<T1,T2>*, std::vector<Node3Disp<T1,T2>*>,
    CompareNodePtr<T1>> queue( cmp );
    
    std::vector<Node3Disp<T1,T2>> txNodes;
    std::vector<bool> inQueue( this->nodes.size(), false );
    std::vector<bool> frozen( this->nodes.size(), false );
    
    initQueue(Tx, t0, queue, txNodes, inQueue, frozen, threadNo);
    
    propagate(queue, inQueue, frozen, threadNo);
    
    if ( traveltimes.size() != Rx.size() ) {
        traveltimes.resize( Rx.size() );
    }
    
    for (size_t nr=0; nr<Rx.size(); ++nr) {
        traveltimes[nr]->resize( Rx[nr]->size() );
        for (size_t n=0; n<Rx[nr]->size(); ++n)
            (*traveltimes[nr])[n] = this->getTraveltime((*Rx[nr])[n], this->nodes, threadNo);
    }
	return 0;
}


template<typename T1, typename T2>
int Grid3Duisp<T1,T2>::raytrace(const std::vector<sxyz<T1>>& Tx,
								const std::vector<T1>& t0,
								const std::vector<sxyz<T1>>& Rx,
								std::vector<T1>& traveltimes,
								std::vector<std::vector<sxyz<T1>>>& r_data,
								const size_t threadNo) const {
    
    if ( this->check_pts(Tx) == 1 ) return 1;
    if ( this->check_pts(Rx) == 1 ) return 1;
    
    for ( size_t n=0; n<this->nodes.size(); ++n ) {
        this->nodes[n].reinit( threadNo );
    }
    
    CompareNodePtr<T1> cmp(threadNo);
    std::priority_queue< Node3Disp<T1,T2>*, std::vector<Node3Disp<T1,T2>*>,
    CompareNodePtr<T1>> queue( cmp );
    
    std::vector<Node3Disp<T1,T2>> txNodes;
    std::vector<bool> inQueue( this->nodes.size(), false );
    std::vector<bool> frozen( this->nodes.size(), false );
	
    initQueue(Tx, t0, queue, txNodes, inQueue, frozen, threadNo);
    
    propagate(queue, inQueue, frozen, threadNo);
    
    if ( traveltimes.size() != Rx.size() ) {
        traveltimes.resize( Rx.size() );
    }
    if ( r_data.size() != Rx.size() ) {
        r_data.resize( Rx.size() );
    }
    for ( size_t ni=0; ni<r_data.size(); ++ni ) {
        r_data[ni].resize( 0 );
    }
    T2 nodeParentRx;
    T2 cellParentRx;
    
    for (size_t n=0; n<Rx.size(); ++n) {
        
        traveltimes[n] = this->getTraveltime(Rx[n], this->nodes, nodeParentRx, cellParentRx,
											 threadNo);
        
		bool flag=false;
		for ( size_t ns=0; ns<Tx.size(); ++ns ) {
			if ( Rx[n] == Tx[ns] ) {
				
				r_data[n].resize( 1 );
				r_data[n][0] = Rx[n];
				
				flag = true;
				break;
			}
		}
		if ( flag ) continue;
		
        // Rx are in nodes (not txNodes)
        std::vector<Node3Disp<T1,T2>> *node_p;
        node_p = &(this->nodes);
        
        std::vector<sxyz<T1>> r_tmp;
        T2 iChild, iParent = nodeParentRx;
        sxyz<T1> child;
		
        // store the son's coord
        child = Rx[n];
        while ( (*node_p)[iParent].getNodeParent(threadNo) !=
               std::numeric_limits<T2>::max() ) {
 			
			r_tmp.push_back( child );
			
			// we now go up in time - parent becomes the child of grand'pa
			iChild = iParent;
			child = (*node_p)[iChild];
            
			// grand'pa is now papa
			iParent = (*node_p)[iChild].getNodeParent(threadNo);
            if ( iParent >= this->nodes.size() ) {
                node_p = &txNodes;
                iParent -= this->nodes.size();
            }
            else {
                node_p = &(this->nodes);
            }
		}
		
		// parent is now at Tx
		r_tmp.push_back( child );
		
		// finally, store Tx position
		child = (*node_p)[iParent];
		r_tmp.push_back( child );
		
        // the order should be from Tx to Rx, so we reorder...
        iParent = static_cast<T2>(r_tmp.size());
        r_data[n].resize( r_tmp.size() );
        for ( size_t nn=0; nn<r_data[n].size(); ++nn ) {
            r_data[n][nn] = r_tmp[ iParent-1-nn ];
        }
    }
	return 0;
}

template<typename T1, typename T2>
int Grid3Duisp<T1,T2>::raytrace(const std::vector<sxyz<T1>>& Tx,
								const std::vector<T1>& t0,
								const std::vector<const std::vector<sxyz<T1>>*>& Rx,
								std::vector<std::vector<T1>*>& traveltimes,
								std::vector<std::vector<std::vector<sxyz<T1>>>*>& r_data,
								const size_t threadNo) const {
    
    if ( this->check_pts(Tx) == 1 ) return 1;
    for ( size_t n=0; n<Rx.size(); ++n )
        if ( this->check_pts(*Rx[n]) == 1 ) return 1;
    
    for ( size_t n=0; n<this->nodes.size(); ++n ) {
        this->nodes[n].reinit( threadNo );
    }
    
    CompareNodePtr<T1> cmp(threadNo);
    std::priority_queue< Node3Disp<T1,T2>*, std::vector<Node3Disp<T1,T2>*>,
    CompareNodePtr<T1>> queue( cmp );
    
    std::vector<Node3Disp<T1,T2>> txNodes;
    std::vector<bool> inQueue( this->nodes.size(), false );
    std::vector<bool> frozen( this->nodes.size(), false );
    
    initQueue(Tx, t0, queue, txNodes, inQueue, frozen, threadNo);
    
    propagate(queue, inQueue, frozen, threadNo);
    
    if ( traveltimes.size() != Rx.size() ) {
        traveltimes.resize( Rx.size() );
    }
    if ( r_data.size() != Rx.size() ) {
        r_data.resize( Rx.size() );
    }
    
    for (size_t nr=0; nr<Rx.size(); ++nr) {
        
        traveltimes[nr]->resize( Rx[nr]->size() );
        r_data[nr]->resize( Rx[nr]->size() );
        for ( size_t ni=0; ni<r_data[nr]->size(); ++ni ) {
            (*r_data[nr])[ni].resize( 0 );
        }
        
        T2 nodeParentRx;
        T2 cellParentRx;
        
        for (size_t n=0; n<Rx[nr]->size(); ++n) {
            
            (*traveltimes[nr])[n] = this->getTraveltime((*Rx[nr])[n], this->nodes,
                                                        nodeParentRx, cellParentRx,
                                                        threadNo);
            
            bool flag=false;
            for ( size_t ns=0; ns<Tx.size(); ++ns ) {
                if ( (*Rx[nr])[n] == Tx[ns] ) {
                    
                    (*r_data[nr])[n].resize( 1 );
                    (*r_data[nr])[n][0] = (*Rx[nr])[n];
                    
                    flag = true;
                    break;
                }
            }
            if ( flag ) continue;
            
            // Rx are in nodes (not txNodes)
            std::vector<Node3Disp<T1,T2>> *node_p;
            node_p = &(this->nodes);
            
            std::vector<sxyz<T1>> r_tmp;
            T2 iChild, iParent = nodeParentRx;
            sxyz<T1> child;
            
            // store the son's coord
            child = (*Rx[nr])[n];
            while ( (*node_p)[iParent].getNodeParent(threadNo) !=
                   std::numeric_limits<T2>::max() ) {
                
                r_tmp.push_back( child );
                
                // we now go up in time - parent becomes the child of grand'pa
                iChild = iParent;
                child = (*node_p)[iChild];
                
                // grand'pa is now papa
                iParent = (*node_p)[iChild].getNodeParent(threadNo);
                if ( iParent >= this->nodes.size() ) {
                    node_p = &txNodes;
                    iParent -= this->nodes.size();
                }
                else {
                    node_p = &(this->nodes);
                }
            }
            
            // parent is now at Tx
            r_tmp.push_back( child );
            
            // finally, store Tx position
            child = (*node_p)[iParent];
            r_tmp.push_back( child );
            
            // the order should be from Tx to Rx, so we reorder...
            iParent = static_cast<T2>(r_tmp.size());
            (*r_data[nr])[n].resize( r_tmp.size() );
            for ( size_t nn=0; nn<(*r_data[nr])[n].size(); ++nn ) {
                (*r_data[nr])[n][nn] = r_tmp[ iParent-1-nn ];
            }
        }
    }
	return 0;
}

template<typename T1, typename T2>
int Grid3Duisp<T1,T2>::raytrace(const std::vector<sxyz<T1>>& Tx,
								const std::vector<T1>& t0,
								const std::vector<sxyz<T1>>& Rx,
								std::vector<T1>& traveltimes,
								std::vector<std::vector<sxyz<T1>>>& r_data,
								std::vector<std::vector<siv<T1>>>& l_data,
								const size_t threadNo) const {
    
	//	std::cout << "   running in thread no " << threadNo << std::endl;
    if ( check_pts(Tx) == 1 ) return 1;
    if ( check_pts(Rx) == 1 ) return 1;
    
    for ( size_t n=0; n<this->nodes.size(); ++n ) {
        this->nodes[n].reinit( threadNo );
    }
    
    CompareNodePtr<T1> cmp(threadNo);
    std::priority_queue< Node3Disp<T1,T2>*, std::vector<Node3Disp<T1,T2>*>,
    CompareNodePtr<T1>> queue( cmp );
    
    std::vector<Node3Disp<T1,T2>> txNodes;
    std::vector<bool> inQueue( this->nodes.size(), false );
    std::vector<bool> frozen( this->nodes.size(), false );
    
    initQueue(Tx, t0, queue, txNodes, inQueue, frozen, threadNo);
    
    propagate(queue, inQueue, frozen, threadNo);
    
    if ( traveltimes.size() != Rx.size() ) {
        traveltimes.resize( Rx.size() );
    }
    if ( l_data.size() != Rx.size() ) {
        l_data.resize( Rx.size() );
    }
    for ( size_t ni=0; ni<l_data.size(); ++ni ) {
        l_data[ni].resize( 0 );
    }
    if ( r_data.size() != Rx.size() ) {
        r_data.resize( Rx.size() );
    }
    for ( size_t ni=0; ni<r_data.size(); ++ni ) {
        r_data[ni].resize( 0 );
    }
    T2 nodeParentRx;
    T2 cellParentRx;
    
    for (size_t n=0; n<Rx.size(); ++n) {
        
        traveltimes[n] = getTraveltime(Rx[n], this->nodes, nodeParentRx, cellParentRx,
									   threadNo);
        
        bool flag=false;
        for ( size_t ns=0; ns<Tx.size(); ++n ) {
            if ( Rx[n] == Tx[ns] ) {
                
                r_data[n].resize( 1 );
                r_data[n][0] = Rx[n];
                
                // no need to update l_data: ray length is zero
                
                flag = true;
            }
        }
        if ( flag ) continue;
		
        // Rx are in nodes (not txNodes)
        std::vector<Node3Disp<T1,T2>> *node_p;
        node_p = &this->nodes;
        
        std::vector<sxyz<T1>> r_tmp;
        T2 iChild, iParent = nodeParentRx;
        sxyz<T1> child;
        siv<T1> cell;
		
        // store the son's coord
        child = Rx[n];
        cell.i = cellParentRx;
        while ( (*node_p)[iParent].getNodeParent(threadNo) !=
               std::numeric_limits<T2>::max() ) {
 			
			r_tmp.push_back( child );
			
			cell.v = (*node_p)[iParent].getDistance( child );
			bool found=false;
			for (size_t nc=0; nc<l_data[n].size(); ++nc) {
				if ( l_data[n][nc].i == cell.i ) {
					l_data[n][nc].v += cell.v;
					found = true;
				}
			}
			if ( found == false ) {
				l_data[n].push_back( cell );
			}
			
			// we now go up in time - parent becomes the child of grand'pa
			iChild = iParent;
			child = (*node_p)[iChild];
            cell.i = (*node_p)[iChild].getCellParent(threadNo);
			
			// grand'pa is now papa
			iParent = (*node_p)[iChild].getNodeParent(threadNo);
            if ( iParent >= this->nodes.size() ) {
                node_p = &txNodes;
                iParent -= this->nodes.size();
            }
            else {
                node_p = &this->nodes;
            }
		}
		
		// parent is now at Tx
		r_tmp.push_back( child );
		
		cell.v = (*node_p)[iParent].getDistance( child );
		bool found=false;
		for (size_t nc=0; nc<l_data[n].size(); ++nc) {
			if ( l_data[n][nc].i == cell.i ) {
				l_data[n][nc].v += cell.v;
				found = true;
			}
		}
		if ( found == false ) {
			l_data[n].push_back( cell );
		}
		
		// finally, store Tx position
		child = (*node_p)[iParent];
		r_tmp.push_back( child );
		
        //  must be sorted to build matrix L
		std::sort(l_data[n].begin(), l_data[n].end(), CompareSiv_i<T1>());
        
        // the order should be from Tx to Rx, so we reorder...
        iParent = static_cast<T2>(r_tmp.size());
        r_data[n].resize( r_tmp.size() );
        for ( size_t nn=0; nn<r_data[n].size(); ++nn ) {
            r_data[n][nn] = r_tmp[ iParent-1-nn ];
        }
    }
    return 0;
}

template<typename T1, typename T2>
void Grid3Duisp<T1,T2>::initQueue(const std::vector<sxyz<T1>>& Tx,
								  const std::vector<T1>& t0,
								  std::priority_queue<Node3Disp<T1,T2>*,
								  std::vector<Node3Disp<T1,T2>*>,
								  CompareNodePtr<T1>>& queue,
								  std::vector<Node3Disp<T1,T2>>& txNodes,
								  std::vector<bool>& inQueue,
								  std::vector<bool>& frozen,
								  const size_t threadNo) const {
    
    for (size_t n=0; n<Tx.size(); ++n) {
        bool found = false;
        for ( size_t nn=0; nn<this->nodes.size(); ++nn ) {
            if ( this->nodes[nn] == Tx[n] ) {
                found = true;
                this->nodes[nn].setTT( t0[n], threadNo );
                queue.push( &(this->nodes[nn]) );
                inQueue[nn] = true;
                frozen[nn] = true;
                break;
            }
        }
        if ( found==false ) {
            // If Tx[n] is not on a node, we create a new node and initialize the queue:
            txNodes.push_back( Node3Disp<T1,T2>(t0[n], Tx[n].x, Tx[n].y, Tx[n].z,
												this->nThreads, threadNo));
            txNodes.back().pushOwner( this->getCellNo(Tx[n]) );
            txNodes.back().setGridIndex( static_cast<T2>(this->nodes.size()+
														 txNodes.size()-1) );
            frozen.push_back( true );
            
            prepropagate(txNodes.back(), queue, inQueue, frozen, threadNo); // See description in the function declaration
            
            //	queue.push( &(txNodes.back()) );	//Don't use if prepropagate is used
            //	inQueue.push_back( true );			//Don't use if prepropagate is used
			
        }
    }
}


template<typename T1, typename T2>
void Grid3Duisp<T1,T2>::prepropagate(const Node3Disp<T1,T2>& node,
									 std::priority_queue<Node3Disp<T1,T2>*,
									 std::vector<Node3Disp<T1,T2>*>,
									 CompareNodePtr<T1>>& queue,
									 std::vector<bool>& inQueue,
									 std::vector<bool>& frozen,
									 size_t threadNo) const {
    
    // This function can be used to "prepropagate" each Tx nodes one first time
    // during "initQueue", before running "propagate".
    // When a Tx source node seems to be lost in the queue and is not
    // propagated, corrupting the entire traveltime table,
    // this function force the propagation of every source points and can
    // solve the problem.
    
    for ( size_t no=0; no<node.getOwners().size(); ++no ) {
        T2 cellNo = node.getOwners()[no];
        for ( size_t k=0; k< this->neighbors[cellNo].size(); ++k ) {
            size_t neibNo = this->neighbors[cellNo][k];
            if ( neibNo == node.getGridIndex() || frozen[neibNo] ) {
                continue;
            }
            
            // compute dt
            T1 dt = this->computeDt(node, this->nodes[neibNo]);
            
            if ( node.getTT( threadNo )+dt < this->nodes[neibNo].getTT( threadNo ) ) {
                this->nodes[neibNo].setTT( node.getTT( threadNo )+dt, threadNo );
                this->nodes[neibNo].setnodeParent( node.getGridIndex(), threadNo );
                this->nodes[neibNo].setCellParent( cellNo, threadNo );
                
                if ( !inQueue[neibNo] ) {
                    queue.push( &(this->nodes[neibNo]) );
                    inQueue[neibNo] = true;
                }
            }
        }
    }
}


template<typename T1, typename T2>
void Grid3Duisp<T1,T2>::propagate(std::priority_queue<Node3Disp<T1,T2>*,
								  std::vector<Node3Disp<T1,T2>*>,
								  CompareNodePtr<T1>>& queue,
								  std::vector<bool>& inQueue,
								  std::vector<bool>& frozen,
								  const size_t threadNo) const {
    
    while ( !queue.empty() ) {
        const Node3Disp<T1,T2>* src = queue.top();
        queue.pop();
        inQueue[ src->getGridIndex() ] = false;
        
        for ( size_t no=0; no<src->getOwners().size(); ++no ) {
            
            T2 cellNo = src->getOwners()[no];
            
            for ( size_t k=0; k< this->neighbors[cellNo].size(); ++k ) {
                T2 neibNo = this->neighbors[cellNo][k];
                if ( neibNo == src->getGridIndex() || frozen[neibNo] ) {
                    continue;
                }
                
                // compute dt
                T1 dt = this->computeDt(*src, this->nodes[neibNo]);
				
                if (src->getTT(threadNo)+dt < this->nodes[neibNo].getTT(threadNo)) {
                    this->nodes[neibNo].setTT( src->getTT(threadNo)+dt, threadNo );
                    this->nodes[neibNo].setnodeParent(src->getGridIndex(),threadNo);
                    this->nodes[neibNo].setCellParent(cellNo, threadNo );
                    
                    if ( !inQueue[neibNo] ) {
                        queue.push( &(this->nodes[neibNo]) );
                        inQueue[neibNo] = true;
                    }
                }
            }
        }
    }
}

template<typename T1, typename T2>
T1 Grid3Duisp<T1,T2>::getTraveltime(const sxyz<T1>& Rx,
									const std::vector<Node3Disp<T1,T2>>& nodes,
									const size_t threadNo) const {
	
    for ( size_t nn=0; nn<nodes.size(); ++nn ) {
        if ( nodes[nn] == Rx ) {
            return nodes[nn].getTT(threadNo);
        }
    }
	//If Rx is not on a node:
    T1 slo = this->computeSlowness( Rx );
	
    T2 cellNo = this->getCellNo( Rx );
    T2 neibNo = this->neighbors[cellNo][0];
    T1 dt = this->computeDt(nodes[neibNo], Rx, slo);
	
    T1 traveltime = nodes[neibNo].getTT(threadNo)+dt;
    for ( size_t k=1; k< this->neighbors[cellNo].size(); ++k ) {
        neibNo = this->neighbors[cellNo][k];
        dt = this->computeDt(nodes[neibNo], Rx, slo);
        if ( traveltime > nodes[neibNo].getTT(threadNo)+dt ) {
            traveltime = nodes[neibNo].getTT(threadNo)+dt;
        }
    }
    return traveltime;
}

template<typename T1, typename T2>
T1 Grid3Duisp<T1,T2>::getTraveltime(const sxyz<T1>& Rx,
									const std::vector<Node3Disp<T1,T2>>& nodes,
									T2& nodeParentRx, T2& cellParentRx,
									const size_t threadNo) const {

    for ( size_t nn=0; nn<nodes.size(); ++nn ) {
        if ( nodes[nn] == Rx ) {
            nodeParentRx = nodes[nn].getNodeParent(threadNo);
            cellParentRx = nodes[nn].getCellParent(threadNo);
            return nodes[nn].getTT(threadNo);
        }
    }
	//If Rx is not on a node:
    T1 slo = this->computeSlowness( Rx );

    T2 cellNo = this->getCellNo( Rx );
    T2 neibNo = this->neighbors[cellNo][0];
    T1 dt = this->computeDt(nodes[neibNo], Rx, slo);

    T1 traveltime = nodes[neibNo].getTT(threadNo)+dt;
    nodeParentRx = neibNo;
    cellParentRx = cellNo;
    for ( size_t k=1; k< this->neighbors[cellNo].size(); ++k ) {
        neibNo = this->neighbors[cellNo][k];
        dt = this->computeDt(nodes[neibNo], Rx, slo);
        if ( traveltime > nodes[neibNo].getTT(threadNo)+dt ) {
            traveltime = nodes[neibNo].getTT(threadNo)+dt;
            nodeParentRx = neibNo;
        }
    }
    return traveltime;
}


#endif
