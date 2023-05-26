# extent-based-fs

A terminal-based file system with an in-house recursive first-fit allocation algorithm.

## Description

I developed a fully functional extent-based file system by successfully implementing every FUSE callback function.  Based on personal testing, all core functionalities work as they should.  However, the only known problem with our file system is that indirection has not been built. Going through our codebase and version history, you will realize that we made as many attempts as time allowed to incorporate indirection. Ultimately, it was a smarter decision to just present what we know for  certain works well, rather than provide a file system that is not  thoroughly tested.  Nonetheless, we are very proud of the file system that we created.
