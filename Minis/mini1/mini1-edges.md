# Edge Sensitivity at Package Level Code

version 0.2a 

Focus: Data structures, memory, and edges 

Welcome to our first research project, mini1 (1 of 2), with a focus
on data structures and memory sensitivity at defined edges. For our 
first mini the edges we are focusing on are within the process. How 
data moves between libraries (internal stacks).

When talking about edges, we are thinking of edge computing. Wait,
if edge computing is moving data to the end-point, how do we achieve
this within a process?


## Why we do minis 

Mini projects are explorations to focus on how things work. Not on
adding functionality. Often we are too focused on meeting capabilities 
and gloss over how our actions affect what we build. 

This rush to complete code leaves two important areas to chance. So,
we are inverting the effort to focus on one to two specific areas of
performance-based, multi-process, computing. Each mini is designed to 
provide research topics  along with developing skills in analysis, 
languages (C/C++ and Python), and analyical testing and verification. 
Opportunities exist to practice reverse engineering, open source code 
evaluation, strong/weak scaling, and performance metric gathering.


## Responsibilities

Minis are organized as a unstructured learning experience. Meaning
there are no set results of specific matched numerical values. 
Consequently, the are more degrees of freedom for you to explore 
and shape your investigations. 

Score (aka points) are awarded by depth of research and validation, 
not by application-like features or LoC.

**The burden of proof is yours to win.** Please be sure to 
take this responsibility seriously. Here are some hints to help
set you on a course of success. 

   * Minis are not applications, features are not required
   * Depth of research and investigation is very important
   * Organization is expected to be professional 
   * Reports are paragraph formatted, and use supported data
     to support conclusions.
   * Do not run/test from IDE's VMs
   * Validation and verification using good practices
   * Realistic use of classes, abstraction, containers, and 
     primative data types


## Resources and Data

Many resources are available for your use. Use of 
There are several resources already uploaded in Canvas to support this 
investigation. They are:

   * Data set: There are many to choose form. I can provide you with one, 
    or consider something from NYC OpenData (https://opendata.cityofnewyork.us)

     What not to use: While the squirrel census of 2018 seems spot on, 
     there wasn’t an update or consecutive years to meet the size 
     requirements. Sad.

     Notes (important ones):
     
        a. The data must have more than 2 million records and a size greater 
           than 12 Gb. Note You will likely need a couple data sets to reach this 
           requirement. 

        b. Do not use the NYC 311 or moving/violation/collisions data sets.


   * Code: The course's non-graded labs are available, consider the basic 
     for organization. 
 
Other sources at your disposal include:

   * Open source code repositories
   * Language and package documentations
   * Research publications and papers
   * Online communities (e.g., stack overflow, xxxx)


## Objectives

There are two phases to the assignment. Each step (phase) is critical
to the success of your project.

You will be asked to document, compare, and contrast design outcomes 
from each phase. Be sure to read about how to present your findings
vs the report.

### Phase 1

   1. Design and code with C/C++ a process to consume the provided 
      data set. Play particular attention to representing the data's
      fields as their primative types (e.g,, integers, floats).

      Create a single project, jam everthing together as if you are
      rushing to get functionality done. Normal stuff.

      Your design must be an Object Oriented Design (Classes), placing 
      emphasise on abstraction, and as a library for long-term use.

      Suggestions, objects, virtual classes, templates, and coding 
      patterns (e.g., facade). Your code should provide features and 
      data through a set of APIs for data reading and basic range 
      searching.
 
      **Do not use threads for this part we are serial processing.**
 
      *Hint: Look at parser.tar.gz for tool suggestions*
      *Hint: What types of searches do you expect?*

   2. Benchmark your code against the data sets. Note think about
      what are reasonable areas where peformance would be a concern.

      Be sure to collect multiple executions (10+) to create an 
      average of your codes' performance. Document both successes and 
      failures in your coding attempts; results are for your report.
 
      **This becomes your baseline for further investigations.**

### Phase 2

   1. Apply parallelization (OpenMP, threading) to your code. Record 
      your results and compare them to your earlier benchmark.

      *Hint: This is where your code harness helps.*

   2. Optimize your code by rewriting your data classes into a 
      parallel otimized libraries. You will need to build libraries
      and then link them to your app (main). 

      Think about:

         a. How to isolate interals of the library from the application. 
         b. How to break file data formatting or grouping into correct 
            data structures.

      Benchmark your results. Be sure to test how each modification 
      affects performance. 

      Remember in this phase, edge performance is the highest goal. 

      *Hint: Do queries shape your design?*


### Language and Tools

The following is a list of languages and tools that are __required__ to
complete the project:

   Languages
   * C/C++, either:
     * gcc/g++, v13 or newer
     * Clang (not apple's xcode) v16 or newer
   * Python3 can be used to generate graphs

   Libraries
   * Boost if needed otherwise: None.

   Tools
   * Cmake (https://cmake.org)

These are suggestions that can help you with the mini:

   * lmod (https://github.com/TACC/Lmod)
   * Linters for C/C++ and Python
   * Editors (2+ recommended): neovim/vim, vscode, IDEs, etc.
   * Terminal: kitty, iterm2, OS native, etc
        ** You may want/need a font (https://www.nerdfonts.com) or two
        ** A shell scripting language (e.g., bash, tcsh,)
              *** https://www.freecodecamp.org/news/bash-scripting-tutorial-linux-shell-script-and-command-line-for-beginners/
              *** https://www.tutorialspoint.com/unix/shell_scripting.htm 
   * Clang analyzers, Valgrind suite, perf, or other like tools


### Restrictions and Limits

As this lab has been constructed with a specific set of learning and
discovery objectives, some aspects are artifically constrained. Yes,
many can be engineered out and exist in the wild yet, yet, the concepts 
remain valid. Therefore, there are several creativity boundaries that 
need to be put in place:

  * No third party libraries or services other than what has been 
    listed above. This means do not use databases. 
       * Parsing tools for reading CSV, and ploting/graphing are okay. 
  * Do not circumvent the spirit of the mini's goals/instructions.
  * AI likely is going to be a big help with learning C/C++ however,
    it should not be used to write your code. 


### What do you need to turn in?

There are three parts to return: 

   1. The code
   2. A report containing results, supporting data, conclusions, and 
      citations/references to support your decisions - facts really help. 
   3. your one-page presentation. This is not a summary presentation! 
      Choose something unique you learned/discovered that you want to tell
      everyone. Uniqueness counts. See below.

These three components should be contained in a archive file, `tar.gz`. 
E.g., teamducks.tar.gz.

One submission per team to canvas. Your team can (re)submit multiple 
times before the due date. **Please do not include test data.**


#### Regarding the presentation and Report

The presention is **exactly one slide** (imagine a poster) to be
presented to the class. The slide should be focusing on a single
important discovery your team made or a key experience you want
to pass to others. 

*Hint: It (slide) should be a key finding by your team. Something 
of significant finding you would like to share. Now, wht it should 
not be: A project summary, a diagram of your classes, a tutorial, 
and most definitely not vague.*

The report is the supporting facts and approach leading to conclusion
and presentation. Please include a discussion on how the team achieved 
a solution, findings/conclusions, citations, and individual contributions.

This is your team's chance to showcase what your research and coding
uncovered. Please include testing results (tabular and graph formats),
and observations backing up your conclusions and recommendations. Lastly, 
don't discard failed attempts, irregardless whether they helped these are
important data points.

*Hint: Supporting your conclusions largely affects your points.*


### What about AI?

We talked about this. Yeah.


### Last bits of advice

Last minute advice and re-iteration of important requirements.

   * Two phases, each should be in their own seperate directories.
     E.g., mini1/part-a, mini1/part-b or however you want to name 
     them. 

   * If you choose to use the NYC data sets - Do not use the 311 or 
     violation/citation/collision data sets. If you do, your mini 
     will be ignored (no points). When in doubt, ask.

   * Start early so you can finish early then you can relax while 
     other teams panic.

   * Do your own work.

   * The presentation is about ONE cool thing you found. IT IS NOT A 
     SUMMARY.
