To incorporate this pass
add ```AU.addRequired<ReachingDefinitions>();``` to ```getAnalysisUsage```

To access the result of this pass
```ReachingDefinitions &RD = getAnalysis<ReachingDefinitions>();```
