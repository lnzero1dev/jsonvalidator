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

#include <AK/HashMap.h>
#include <AK/JsonValue.h>
#include <AK/OwnPtr.h>
#include <LibJsonValidator/Forward.h>
#include <stdio.h>

namespace JsonValidator {

class Parser {
public:
    Parser();
    ~Parser();

    JsonValue run(const FILE* fd);
    JsonValue run(const String filename);
    JsonValue run(const JsonValue& json);

    OwnPtr<JsonSchemaNode>& root_node() { return m_root_node; }

    bool parse_sub_schema(const String& property,
        const JsonObject& json_object,
        JsonSchemaNode* node,
        Function<void(const String&, NonnullOwnPtr<JsonSchemaNode>&&)> callback);

private:
    OwnPtr<JsonSchemaNode> m_root_node;
    OwnPtr<JsonSchemaNode> get_typed_node(const JsonValue&, JsonSchemaNode* parent = nullptr);

    void add_parser_error(String);
    Vector<String> m_parser_errors;

    HashMap<String, JsonSchemaNode*> m_anchors;
};
}
