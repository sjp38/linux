Following 3 patches implement the page-granularity idleness monitoring
primitives.  The 8th patch again makes some functions of the physical address
space monitoring primitives reusable, and the 9th patch implements arbitrary
monitoring target type, so that DAMON can monitor accesses in page granularity
without 'struct damon_region' space overhead.  Finally, 10th patch implements
the primitives for this use case.
