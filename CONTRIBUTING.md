<!-- omit in toc -->
# Contributing to aOS

First off, thanks for taking the time to contribute! ❤️

All types of contributions are encouraged and valued. See the [Table of Contents](#table-of-contents) for different ways to help and details about how this project handles them. Please make sure to read the relevant section before making your contribution. It will make it a lot easier for us maintainers and smooth out the experience for all involved. The community looks forward to your contributions. 🎉

> And if you like the project, but just don't have time to contribute, that's fine. There are other easy ways to support the project and show your appreciation, which we would also be very happy about:
>
> - Star the project
> - Tweet about it
> - Refer this project in your project's readme
> - Mention the project at local meetups and tell your friends/colleagues

<!-- omit in toc -->
## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [I Have a Question](#i-have-a-question)
  - [I Want To Contribute](#i-want-to-contribute)
  - [Reporting Bugs](#reporting-bugs)
  - [Suggesting Enhancements](#suggesting-enhancements)
  - [Your First Code Contribution](#your-first-code-contribution)
  - [Improving The Documentation](#improving-the-documentation)
- [Styleguides](#styleguides)
  - [Commit Messages](#commit-messages)
- [Join The Project Team](#join-the-project-team)

## Code of Conduct

This project and everyone participating in it is governed by the
[aOS Code of Conduct](https://github.com/axrxvm/aOS/blob/CODE_OF_CONDUCT.md).
By participating, you are expected to uphold this code. Please report unacceptable behavior
to <axrxvm@proton.me>.

## I Have a Question

Before you ask a question, it is best to search for existing [Issues](https://github.com/axrxvm/aOS/issues) that might help you. In case you have found a suitable issue and still need clarification, you can write your question in this issue. It is also advisable to search the internet for answers first.

If you then still feel the need to ask a question and need clarification, we recommend the following:

- Open an [Issue](https://github.com/axrxvm/aOS/issues/new).
- Provide as much context as you can about what you're running into.
- Provide project and platform versions (nodejs, npm, etc), depending on what seems relevant.

We will then take care of the issue as soon as possible.

<!--
You might want to create a separate issue tag for questions and include it in this description. People should then tag their issues accordingly.

Depending on how large the project is, you may want to outsource the questioning, e.g. to Stack Overflow or Gitter. You may add additional contact and information possibilities:
- IRC
- Slack
- Gitter
- Stack Overflow tag
- Blog
- FAQ
- Roadmap
- E-Mail List
- Forum
-->

## I Want To Contribute

> ### Legal Notice <!-- omit in toc -->
>
> When contributing to this project, you must agree that you have authored 100% of the content, that you have the necessary rights to the content and that the content you contribute may be provided under the project licence.

### Reporting Bugs

<!-- omit in toc -->
#### Before Submitting a Bug Report

A good bug report shouldn't leave others needing to chase you up for more information. Therefore, we ask you to investigate carefully, collect information and describe the issue in detail in your report. Please complete the following steps in advance to help us fix any potential bug as fast as possible.

- Make sure that you are using the latest version.
- Determine if your bug is really a bug and not an error on your side e.g. using incompatible environment components/versions If you are looking for support, you might want to check [this section](#i-have-a-question)).
- To see if other users have experienced (and potentially already solved) the same issue you are having, check if there is not already a bug report existing for your bug or error in the [bug tracker](https://github.com/axrxvm/aOS/issues?q=label%3Abug).
- Also make sure to search the internet (including Stack Overflow) to see if users outside of the GitHub community have discussed the issue.
- Collect information about the bug:
  - Stack trace (Traceback)
  - OS, Platform and Version (Windows, Linux, macOS, x86, ARM)
  - Version of the interpreter, compiler, SDK, runtime environment, package manager, depending on what seems relevant.
  - Possibly your input and the output
  - Can you reliably reproduce the issue? And can you also reproduce it with older versions?

<!-- omit in toc -->
#### How Do I Submit a Good Bug Report?

> You must never report security related issues, vulnerabilities or bugs including sensitive information to the issue tracker, or elsewhere in public. Instead sensitive bugs must be sent by email to <axrxvm@proton.me>.
<!-- You may add a PGP key to allow the messages to be sent encrypted as well. -->

We use GitHub issues to track bugs and errors. If you run into an issue with the project:

- Open an [Issue](https://github.com/axrxvm/aOS/issues/new). (Since we can't be sure at this point whether it is a bug or not, we ask you not to talk about a bug yet and not to label the issue.)
- Explain the behavior you would expect and the actual behavior.
- Please provide as much context as possible and describe the *reproduction steps* that someone else can follow to recreate the issue on their own. This usually includes your code. For good bug reports you should isolate the problem and create a reduced test case.
- Provide the information you collected in the previous section.

Once it's filed:

- The project team will label the issue accordingly.
- A team member will try to reproduce the issue with your provided steps. If there are no reproduction steps or no obvious way to reproduce the issue, the team will ask you for those steps and mark the issue as `needs-repro`. Bugs with the `needs-repro` tag will not be addressed until they are reproduced.
- If the team is able to reproduce the issue, it will be marked `needs-fix`, as well as possibly other tags (such as `critical`), and the issue will be left to be [implemented by someone](#your-first-code-contribution).

<!-- You might want to create an issue template for bugs and errors that can be used as a guide and that defines the structure of the information to be included. If you do so, reference it here in the description. -->

### Suggesting Enhancements

This section guides you through submitting an enhancement suggestion for aOS, **including completely new features and minor improvements to existing functionality**. Following these guidelines will help maintainers and the community to understand your suggestion and find related suggestions.

<!-- omit in toc -->
#### Before Submitting an Enhancement

- Make sure that you are using the latest version.
- Perform a [search](https://github.com/axrxvm/aOS/issues) to see if the enhancement has already been suggested. If it has, add a comment to the existing issue instead of opening a new one.
- Find out whether your idea fits with the scope and aims of the project. It's up to you to make a strong case to convince the project's developers of the merits of this feature. Keep in mind that we want features that will be useful to the majority of our users and not just a small subset. If you're just targeting a minority of users, consider writing an add-on/plugin library.

<!-- omit in toc -->
#### How Do I Submit a Good Enhancement Suggestion?

Enhancement suggestions are tracked as [GitHub issues](https://github.com/axrxvm/aOS/issues).

- Use a **clear and descriptive title** for the issue to identify the suggestion.
- Provide a **step-by-step description of the suggested enhancement** in as many details as possible.
- **Describe the current behavior** and **explain which behavior you expected to see instead** and why. At this point you can also tell which alternatives do not work for you.
- You may want to **include screenshots or screen recordings** which help you demonstrate the steps or point out the part which the suggestion is related to. You can use [LICEcap](https://www.cockos.com/licecap/) to record GIFs on macOS and Windows, and the built-in [screen recorder in GNOME](https://help.gnome.org/users/gnome-help/stable/screen-shot-record.html.en) or [SimpleScreenRecorder](https://github.com/MaartenBaert/ssr) on Linux. <!-- this should only be included if the project has a GUI -->
- **Explain why this enhancement would be useful** to most aOS users. You may also want to point out the other projects that solved it better and which could serve as inspiration.

<!-- You might want to create an issue template for enhancement suggestions that can be used as a guide and that defines the structure of the information to be included. If you do so, reference it here in the description. -->

### Your First Code Contribution

Ready to contribute code to aOS? Here's how to get started:

#### Development Environment Setup

1. **Prerequisites**:
   - GCC cross-compiler for i386 (`gcc` with `-m32` support)
   - NASM assembler
   - GNU Make
   - QEMU for testing (`qemu-system-i386`)
   - GRUB tools for creating bootable ISOs
   - Git for version control

2. **Build the project**:

   ```bash
   git clone https://github.com/axrxvm/aOS.git
   cd aOS
   make iso
   ```

3. **Test your build**:

   ```bash
   make run          # Run with VGA + serial output
   make run-s        # Run with 50MB disk image
   make run-debug    # Enable QEMU debugging
   ```

#### Making Your First Contribution

1. **Find an issue**: Look for issues tagged with `good first issue` or `help wanted`
2. **Fork the repository**: Create your own fork on GitHub
3. **Create a branch**: `git checkout -b feature/my-contribution`
4. **Follow the code style**:
   - Use `snake_case` for functions and variables
   - K&R style braces (opening brace on same line)
   - 4 spaces for indentation (no tabs)
   - Add the required AOS header to all new files (see project documentation)
5. **Test thoroughly**: Run `make run` and verify serial output
6. **Commit your changes**: Follow the commit message guidelines below
7. **Push to your fork**: `git push origin feature/my-contribution`
8. **Open a Pull Request**: Describe your changes clearly

#### Code Review Process

- Maintainers will review your PR and may request changes
- Address feedback promptly and push updates to your branch
- Once approved, a maintainer will merge your contribution

### Improving The Documentation

Good documentation is crucial for an OS project. Here's how you can help:

#### Types of Documentation Improvements

- **Code Comments**: Add or improve inline comments explaining complex algorithms or hardware interactions
- **Header Documentation**: Document function parameters, return values, and side effects
- **README Updates**: Keep the main README.md accurate and up-to-date
- **Architecture Docs**: Improve explanations of system architecture, memory layout, or subsystem design
- **Tutorial Content**: Create guides for common tasks or new feature usage
- **API Documentation**: Document kernel APIs, syscalls, and VFS operations

#### Documentation Standards

- Use clear, concise language
- Include code examples where helpful
- Keep technical accuracy paramount
- Update version numbers when relevant
- Verify that examples actually work in the current codebase

#### Submitting Documentation Changes

Documentation improvements follow the same process as code contributions:

1. Fork the repository
2. Make your changes
3. Submit a pull request with a clear description of the improvements

## Styleguides

### Commit Messages

Good commit messages help maintain project history and make code review easier.

#### Format

```
<type>: <short summary in present tense>

<optional detailed description>

<optional footer with issue references>
```

#### Types

- `feat`: New feature (e.g., "feat: add TCP socket support")
- `fix`: Bug fix (e.g., "fix: resolve page fault in VFS lookup")
- `docs`: Documentation changes (e.g., "docs: update memory map in README")
- `refactor`: Code restructuring without behavior change
- `perf`: Performance improvements
- `test`: Adding or updating tests
- `build`: Changes to build system or dependencies
- `chore`: Maintenance tasks (e.g., "chore: update version to 0.8.5")

#### Guidelines

- Keep the summary line under 72 characters
- Use present tense ("add feature" not "added feature")
- Don't end the summary with a period
- Reference issue numbers in the footer (e.g., "Fixes #123")
- Explain *what* and *why*, not *how* (the code shows how)

#### Examples

```
feat: implement ARP cache for network stack

Adds a hash-table based ARP cache to reduce network overhead.
Entries expire after 20 minutes to handle IP changes.

Related to #45
```

```
fix: prevent kernel panic on division by zero

Adds exception handler for ISR 0 to catch divide errors
instead of triple-faulting.

Fixes #78
```

## Join The Project Team

Interested in becoming a regular contributor or maintainer?

#### Path to Maintainership

1. **Start contributing**: Make quality contributions over time
2. **Build expertise**: Demonstrate deep understanding of specific subsystems
3. **Help others**: Review PRs, answer questions, and assist other contributors
4. **Show commitment**: Consistent, reliable participation in the project

#### Roles

- **Contributors**: Anyone who submits accepted pull requests
- **Maintainers**: Trusted contributors with merge rights and code review responsibilities
- **Core Team**: Maintainers who guide project direction and architecture decisions

#### Expectations for Team Members

- Follow the Code of Conduct
- Review pull requests promptly and constructively
- Help triage and resolve issues
- Communicate major changes before implementation
- Mentor new contributors

If you're interested in a more formal role, reach out to the project maintainer at <axrxvm@proton.me> after demonstrating sustained contributions.
