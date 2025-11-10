#ifndef NM_PERSISTENCE_H
#define NM_PERSISTENCE_H

/*
 * Persistence Functions for Name Server
 * Save and load file registry to/from disk
 */

// Save the current file registry to disk (requires g_system_mutex)
void save_registry_to_disk_unsafe();

// Load the file registry from disk (called at startup, no mutex needed)
void load_registry_from_disk();

#endif // NM_PERSISTENCE_H
