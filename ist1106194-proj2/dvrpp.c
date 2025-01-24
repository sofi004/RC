/******************************************************************************\
* Distance vector routing protocol with reverse path poisoning.                *
\******************************************************************************/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "routing-simulator.h"

// Message format to send between nodes.
typedef struct {
  // Array to store the cost of reaching each node.
  cost_t costs[MAX_NODES];
  // Array to store the next hops for each target node
  node_t next_hops[MAX_NODES];
} data_t;

// State format.
typedef struct state_t {
  // Matrix to store the costs between nodes.
  cost_t distance_matrix[MAX_NODES][MAX_NODES];
  // Matrix to store the next hop nodes between all pairs of nodes
  node_t next_hops[MAX_NODES][MAX_NODES];
} state_t;

// Handler for the node to allocate and initialize its state.
state_t *init_state() {
  int src = 0;
  state_t *state = (state_t *)calloc(1, sizeof(state_t));
  // Loop through all source nodes.
  do {
    int dest = 0;
    // Loop through all destinations.
    do {
      // Set the cost to reach itself as 0, and other nodes as infinity.
      state->distance_matrix[src][dest] = (src == dest) ? 0 : COST_INFINITY;
      // Initialize next hops with an invalid value (MAX_NODES represents no valid hop).
      state->next_hops[src][dest] = MAX_NODES;
      dest++;
    } while (dest < MAX_NODES);
    src++;
  } while (src < MAX_NODES);
  // Return the initialized state.
  return state;
}

// Send updated distance vector to neighbors.
void send_updates(state_t *state, node_t this_node) {
  // Allocate memory for the message.
  data_t *new_message = (data_t *)malloc(sizeof(data_t));
  node_t adj = 0;
  // Loop through all nighbors.
  while (adj < MAX_NODES) {
    // Check if the neighbor is valid.
    if (adj != this_node && get_link_cost(adj) != COST_INFINITY) {
      // Copy the information to send.
      memcpy(new_message->costs, state->distance_matrix[this_node], MAX_NODES * sizeof(cost_t));
      memcpy(new_message->next_hops, state->next_hops[this_node], MAX_NODES * sizeof(node_t));
      // Prevent advertising a route back to the same neighbor.
      node_t target = 0;
      // Loop through all nighbors.
      while (target < MAX_NODES) {
        if (new_message->next_hops[target] == adj) {
          new_message->costs[target] = COST_INFINITY;
        }
        target++;
      }
      // Send the message to the neighbor.
      send_message(adj, new_message, sizeof(data_t));
    }
    adj++;
  }
}

// Notify a node that a neighboring link has changed cost.
void notify_link_change(node_t neighbor, cost_t new_cost) {
  node_t this_node = get_current_node();
  state_t *state = get_state();
  int has_updates = 0;
  node_t target = 0;
  // Loop through all possible target nodes.
  while (target < MAX_NODES) {
    // Skip itself.
    if (target == this_node) {
      target++;
      continue;
    }
    node_t best_next_hop = this_node;
    // Initialize the lowest cost to reach the target as infinity.
    cost_t lowest_cost = COST_INFINITY;
    // Initialize the best next hop as the node where we are.
    node_t adj = 0;
    // Loop through all possible adjacent nodes to find the best route to the target.
    while (adj < MAX_NODES) {
      // Get the cost to the neighbor.
      cost_t link_adj_cost = get_link_cost(adj);
      // Initialize potential cost.
      cost_t potential_cost = COST_INFINITY;
      switch (adj == neighbor) {
        // If the adjacent node is the neighbor, calculate the potential cost as the new cost plus the distance from the neighbor to the target.
        case 1:
          potential_cost = COST_ADD(new_cost, state->distance_matrix[adj][target]);
          break;
        // If the adjacent node is neither the neighbor nor the current node, calculate the potential cost as the link cost to the adjacent node plus the distance from the adjacent to the target.
        default:
          if (adj != this_node && link_adj_cost != COST_INFINITY) {
            potential_cost = COST_ADD(link_adj_cost, state->distance_matrix[adj][target]);
          }
      }
      // Update the lowest cost and best next hop if a lower cost is calculated.
      if (potential_cost < lowest_cost) {
        best_next_hop = adj;
        lowest_cost = potential_cost;
      }
      adj++;
    }
    // If the cost to the target node has changed, update the distance matrix.
    if (state->distance_matrix[this_node][target] != lowest_cost) {
      // Update the route with the new best next hop.
      set_route(target, best_next_hop, lowest_cost);
      state->distance_matrix[this_node][target] = lowest_cost;
      state->next_hops[this_node][target] = best_next_hop;
      // Mark the flag, because updates have been made.
      has_updates = 1;
    }
    target++;
  }
  // If any updates were made, send updated information to adjacent nodes.
  if (has_updates) {
    send_updates(state, this_node);
  }
}

// Receive a message sent by a neighboring node.
void notify_receive_message(node_t sender, void *message, size_t length) {
  data_t *incoming_message = (data_t *)message;
  state_t *state = get_state();
  // Update the local distance matrix with the received data.
  memcpy(state->distance_matrix[sender], incoming_message->costs, MAX_NODES * sizeof(cost_t));
  memcpy(state->next_hops[sender], incoming_message->next_hops, MAX_NODES * sizeof(node_t));
  int update_flag = 0;
  node_t target = 0;
  node_t this_node = get_current_node();
  // Loop through all possible target nodes to check if any route needs updating.
  while (target < MAX_NODES) {
    // Skip itself.
    if (target == this_node) {
      target++;
      continue;
    }
    // Initialize the best next hop.
    node_t best_next_hop = this_node;
    // Initialize the minimum distance to the target as infinity.
    cost_t min_distance = COST_INFINITY;
    node_t adj = 0;
    // Loop through all possible adjacent nodes to find the best route to the target.
    while (adj < MAX_NODES) {
      // Calculate the cost of reaching the target through the adjacent node.
      if (get_link_cost(adj) != COST_INFINITY && adj != this_node) {
        cost_t calculated_cost = COST_ADD(get_link_cost(adj), state->distance_matrix[adj][target]);
        if (calculated_cost < min_distance) {
          // Update the best next hop if a lower cost is found.
          best_next_hop = adj;
          // Update the minimum distance if a lower cost is found.
          min_distance = calculated_cost;
        }
      }
      adj++;
    }
    // If the cost to the target node has changed, update the distance matrix.
    if (state->distance_matrix[this_node][target] != min_distance) {
      // Set the new route for the target.
      set_route(target, best_next_hop, min_distance);
      state->distance_matrix[this_node][target] = min_distance;
      state->next_hops[this_node][target] = best_next_hop;
      // Mark the flag, because  updates have been made.
      update_flag = 1;
    }
    target++;
  }
  // If any updates were made, send the updated information to adjacent nodes.
  if (update_flag) {
    send_updates(state, this_node);
  }
}
