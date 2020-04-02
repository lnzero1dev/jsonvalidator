/*
 * Copyright (c) 2020, Emanuel Sprung <emanuel.sprung@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <AK/JsonValue.h>
#include <LibJsonValidator/Forward.h>
#include <stdio.h>

namespace JsonValidator {

class ValidationError {
public:
    ValidationError() = default;
    ~ValidationError() = default;

    void add(const String& error)
    {
        m_errors.append(error);
    }

    template<class... Args>
    void addf(const char* fmt, Args... args)
    {
        StringBuilder b;
        b.appendf(fmt, forward<Args>(args)...);
        add(b.build());
    }

    void append(const ValidationError& e)
    {
        for (auto& error : e.errors()) {
            add(error);
        }
    }

    const Vector<String>& errors() const { return m_errors; }

    bool has_error() const { return m_errors.size(); }

private:
    Vector<String> m_errors;
};

struct ValidationResult {
    ValidationError e;
    bool success;
};

class Validator {
public:
    Validator() = default;
    ~Validator() = default;

    ValidationResult run(const Parser&, const FILE* fd);
    ValidationResult run(const Parser&, const String& filename);
    ValidationResult run(const Parser&, const JsonValue& json);
};

}
