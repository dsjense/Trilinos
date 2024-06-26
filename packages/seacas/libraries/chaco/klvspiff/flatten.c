/*
 * Copyright(C) 1999-2020, 2023 National Technology & Engineering Solutions
 * of Sandia, LLC (NTESS).  Under the terms of Contract DE-NA0003525 with
 * NTESS, the U.S. Government retains certain rights in this software.
 *
 * See packages/seacas/LICENSE for details
 */

#include "defs.h"    // for FALSE, TRUE
#include "smalloc.h" // for sfree, smalloc
#include "structs.h" // for vtx_data

int flatten(struct vtx_data  **graph,       /* array of vtx data for graph */
            int                nvtxs,       /* number of vertices in graph */
            int                nedges,      /* number of edges in graph */
            struct vtx_data ***pcgraph,     /* coarsened version of graph */
            int               *pcnvtxs,     /* number of vtxs in coarsened graph */
            int               *pcnedges,    /* number of edges in coarsened graph */
            int              **pv2cv,       /* pointer to v2cv */
            int                using_ewgts, /* are edge weights being used? */
            int                igeom,       /* dimensions of geometric data */
            float            **coords,      /* coordinates for vertices */
            float            **ccoords      /* coordinates for coarsened vertices */
)
{
  double Thresh; /* minimal acceptable size reduction */
  int   *v2cv;   /* map from vtxs to coarse vtxs */
  int    cnvtxs; /* number of vertices in flattened graph */

  Thresh = .9;

  v2cv = smalloc((nvtxs + 1) * sizeof(int));

  find_flat(graph, nvtxs, &cnvtxs, v2cv);

  if (cnvtxs <= Thresh * nvtxs) { /* Sufficient shrinkage? */
    makefgraph(graph, nvtxs, nedges, pcgraph, cnvtxs, pcnedges, v2cv, using_ewgts, igeom, coords,
               ccoords);

    *pcnvtxs = cnvtxs;
    *pv2cv   = v2cv;
    return (TRUE);
  }

  /* Not worth bothering */
  sfree(v2cv);
  return (FALSE);
}

void find_flat(struct vtx_data **graph,   /* data structure for storing graph */
               int               nvtxs,   /* number of vertices in graph */
               int              *pcnvtxs, /* number of coarse vertices */
               int              *v2cv     /* map from vtxs to coarse vtxs */
)
{
  /* Look for cliques with the same neighbor set.  These are matrix */
  /* rows corresponding to multiple degrees of freedom on a node. */
  /* They can be flattened out, generating a smaller graph. */

  int *scatter;   /* for checking neighbor list identity */
  int *hash;      /* hash value for each vertex */
  int  this_hash; /* particular hash value */
  int  neighbor;  /* neighbor of a vertex */
  int  cnvtxs;    /* number of distinct vertices */
  int  i, j;      /* loop counters */

  hash    = smalloc((nvtxs + 1) * sizeof(int));
  scatter = smalloc((nvtxs + 1) * sizeof(int));

  /* compute hash values */

  for (i = 1; i <= nvtxs; i++) {
    this_hash = i;
    for (j = 1; j < graph[i]->nedges; j++) {
      this_hash += graph[i]->edges[j];
    }
    hash[i] = this_hash;
  }

  for (i = 1; i <= nvtxs; i++) {
    v2cv[i]    = 0;
    scatter[i] = 0;
  }

  /* Find supernodes. */
  cnvtxs = 0;

  for (i = 1; i <= nvtxs; i++) {
    if (v2cv[i] == 0) { /* Not yet flattened. */
      v2cv[i] = ++cnvtxs;
      for (j = 1; j < graph[i]->nedges; j++) {
        neighbor = graph[i]->edges[j];
        if (neighbor > i && hash[neighbor] == hash[i] &&   /* same hash value */
            graph[i]->nedges == graph[neighbor]->nedges && /* same degree */
            v2cv[neighbor] == 0 &&                         /* neighbor not flattened */
            SameStructure(i, neighbor, graph, scatter)) {
          v2cv[neighbor] = cnvtxs;
        }
      }
    }
  }

  *pcnvtxs = cnvtxs;

  sfree(scatter);
  sfree(hash);
}

int SameStructure(int node1, int node2,     /* two vertices which might have same nonzeros */
                  struct vtx_data **graph,  /* data structure for storing graph */
                  int              *scatter /* array for checking vertex labelling */
)
{
  int same; /* are two vertices indistinguisable? */
  int i;    /* loop counter */

  scatter[node1] = node1;
  for (i = 1; i < graph[node1]->nedges; i++) {
    scatter[graph[node1]->edges[i]] = node1;
  }

  for (i = 1; i < graph[node2]->nedges; i++) {
    if (scatter[graph[node2]->edges[i]] != node1) {
      break;
    }
  }
  same = (i == graph[node2]->nedges && scatter[node2] == node1);

  return (same);
}
