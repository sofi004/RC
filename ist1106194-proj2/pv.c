/******************************************************************************\
* Path vector routing protocol.                                                *
\******************************************************************************/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "routing-simulator.h"

// Message format to send between nodes.
typedef struct {
} data_t;

// State format.
typedef struct state_t {
} state_t;

// Handler for the node to allocate and initialize its state.
state_t *init_state() {
  state_t *state = (state_t *)calloc(1, sizeof(state_t));
  return state;
}

// Notify a node that a neighboring link has changed cost.
void notify_link_change(node_t neighbor, cost_t new_cost) {}

// Receive a message sent by a neighboring node.
void notify_receive_message(node_t sender, void *message, size_t length) {}
