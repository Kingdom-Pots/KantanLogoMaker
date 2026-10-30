# Performance roadmap progress

The render benchmark now covers simple (1 object), medium (24 objects), and dense (96 visible objects) scenes,
with outlines, shadows, and bevels. Each output size receives a warmup and 21 measured renders. CSV output reports
the median, nearest-rank 95th percentile, maximum, and sample count. The existing milliseconds column remains
available to measurement scripts and now represents the median.

Each measured output is released before checking the memory budget. The run fails if budgeted render memory
remains allocated. This catches retained render allocations; it does not replace a long editor/history memory soak.

Reproduce by building kantan_bench and running its Release executable. The measured results from this pass are in
out/roadmap-render-profile.csv. Build evidence is in out/roadmap-benchmark-build.log.

The medium scene's 512 px renders were around 21 ms median; 1024 px renders were around 67 ms median in the first
pass. This supports prioritizing interactive render scheduling for larger scenes. It is not a measurement of mouse
input latency or a claim that rendering performance has improved.

Next implementation work: measure actual canvas gesture latency, coalesce pending previews, and use a bounded
interactive preview resolution while preserving full-resolution rendering after release and during export. Before
shipping that change, verify that previews cannot overwrite newer states and that export quality is unchanged.
Text/image/boolean workloads and an edit/undo memory soak remain separate roadmap checks.
