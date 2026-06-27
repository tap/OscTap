/*
  oscpack -- Open Sound Control (OSC) packet manipulation library
    http://www.rossbencina.com/code/oscpack

    Copyright (c) 2004-2013 Ross Bencina <rossb@audiomulch.com>

  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files
  (the "Software"), to deal in the Software without restriction,
  including without limitation the rights to use, copy, modify, merge,
  publish, distribute, sublicense, and/or sell copies of the Software,
  and to permit persons to whom the Software is furnished to do so,
  subject to the following conditions:

  The above copyright notice and this permission notice shall be
  included in all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
  ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
  CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
  WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
#ifndef INCLUDED_OSCTAP_SMALLSTRING_H
#define INCLUDED_OSCTAP_SMALLSTRING_H

/*
  Intentionally empty.

  This header was a placeholder in a fork's small-string-optimisation experiment
  and never carried a definition. OscTap's outbound stream now uses
  std::string_view (plus a const char* overload), so nothing here is needed. The
  file is retained only so the public include path -- and its <oscpack/...>
  compatibility shim -- keep resolving for any downstream code that still
  #includes it. Audit finding #6 ("empty SmallString.h") closed: it is now an
  explicit, guarded no-op rather than a zero-byte mystery file.
*/

#endif /* INCLUDED_OSCTAP_SMALLSTRING_H */
