\defgroup Immediate Immediate Mode

\TODO Describe the immediate mode.

**Advice.**
When implementing algorithms, one may want to turn off immediate mode to elide
a block swap between a pair of `foreach` operations. When doing so, one should
not only preserve and restore the old state, but also respect it for the final
`foreach` operations executed by the algorithm. In other words, if
`Master` was not in the immediate mode when the algorithm was started, the
final operations should remain in the queue, rather than being finalized
immediately. (This allows multiple such algorithms to be chained together,
while still minimizing block movement.) The simplest way to achieve this is to
simply save and restore the immediate mode. If the mode is switched from
non-immediate to immediate, the command queue is flushed automatically.

~~~{.cpp}
bool original = master.immediate();

...

master.set_immediate(original);
~~~
