// Mutual exclusion lock.
// TODO: what does the "Mutual exclusion lock" mean and how does it work?
struct spinlock {
  uint locked;       // Is the lock held?

  // For debugging:
  char *name;        // Name of lock.
  struct cpu *cpu;   // The cpu holding the lock.
};

