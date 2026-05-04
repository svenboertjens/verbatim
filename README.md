# Verbatim

A compiler backend project focusing on predictable codegen.

I'm building this from scratch with not much prior knowledge in compilers, so it's a learning experience as much as it is something I hope turns out what I want it to be.

The push for this project and the predictability goal was when in another project of mine, both GCC and Clang attempted to be helpful and regressed a hot loop significantly.

This project is still heavily in progress. The IR isn't set in stone yet, and some optimization passes are being worked on mainly to stress the IR. Codegen currently doesn't exist yet, but will be worked on once I'm more confident about the IR itself.

This is also my first time working with C++, so there may be bugs that Clangd hasn't caught yet.

