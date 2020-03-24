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

#include "Forward.h"
#include <AK/HashMap.h>
#include <AK/JsonValue.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/NonnullOwnPtrVector.h>
#include <AK/OwnPtr.h>
#include <AK/String.h>

#ifndef __serenity__
#    include <regex.h>
#endif
namespace JsonValidator {

class ValidationError;

enum class InstanceType : u8 {
    Undefined,
    Null,
    Boolean,
    Object,
    Array,
    Number,
    String,
};

String to_string(InstanceType type);

class JsonSchemaNode {
public:
    virtual ~JsonSchemaNode();
    virtual const char* class_name() const = 0;
    virtual void dump(int indent, String additional = "") const;

    virtual bool validate(const JsonValue&, ValidationError& e) const;

    void set_default_value(JsonValue default_value)
    {
        m_default_value = default_value;
    }

    void set_id(String id)
    {
        m_id = id;
    }

    void set_type(InstanceType type)
    {
        m_type = type;
    }

    void set_required(bool required)
    {
        m_required = required;
    }

    void set_enum_items(JsonValue enum_items)
    {
        m_enum_items = enum_items;
    }

    void set_identified_by_pattern(bool identified_by_pattern, const String pattern)
    {
        m_identified_by_pattern = identified_by_pattern;
        m_pattern = pattern;
#ifndef __serenity__
        if (regcomp(&m_pattern_regex, pattern.characters(), REG_EXTENDED)) {
            perror("regcomp");
        }
#endif
    }

    void append_all_of(NonnullOwnPtr<JsonSchemaNode>&& node)
    {
        m_all_of.append(move(node));
    }

    bool identified_by_pattern() const { return m_identified_by_pattern; }

    bool match_against_pattern(const String value) const
    {
#ifdef __serenity__
        UNUSED_PARAM(value);
        if (m_pattern == "^.*$") {
            // FIXME: Match everything, to be replaced with below code from else case when
            // posix pattern matching implemented
            return true;
        }
#else
        int reti = regexec(&m_pattern_regex, value.characters(), 0, NULL, 0);
        if (!reti) {
            return true;
        } else if (reti == REG_NOMATCH) {
        } else {
            char buf[100];
            regerror(reti, &m_pattern_regex, buf, sizeof(buf));
            fprintf(stderr, "Regex match failed: %s\n", buf);
        }
#endif
        return false;
    }

    bool required() const { return m_required; }
    InstanceType type() const { return m_type; }
    const String id() const { return m_id; }
    JsonValue default_value() const { return m_default_value; }
    JsonValue enum_items() const { return m_enum_items; }

    JsonSchemaNode* parent() const { return m_parent; }
    String path() const;

protected:
    JsonSchemaNode() {}
    JsonSchemaNode(JsonSchemaNode* parent)
        : m_parent(parent)
    {
    }
    JsonSchemaNode(JsonSchemaNode* parent, String id, InstanceType type)
        : m_id(move(id))
        , m_type(type)
        , m_parent(parent)
    {
    }
    JsonSchemaNode(String id, InstanceType type)
        : m_id(move(id))
        , m_type(type)
    {
    }

    bool m_required { false };

private:
    String m_id;
    InstanceType m_type;
    JsonValue m_default_value;
    JsonValue m_enum_items;
    bool m_identified_by_pattern { false };
    String m_pattern;
#ifndef __serenity__
    regex_t m_pattern_regex;
#endif
    JsonSchemaNode* m_parent;

    NonnullOwnPtrVector<JsonSchemaNode> m_all_of;
};

class StringNode : public JsonSchemaNode {
public:
    StringNode(JsonSchemaNode* parent, String id)
        : JsonSchemaNode(parent, id, InstanceType::String)
    {
    }

    virtual void dump(int indent, String additional) const override;
    virtual bool validate(const JsonValue&, ValidationError&) const override;

    void set_pattern(const String& pattern)
    {
        printf("Set pattern: %s\n", pattern.characters());
        m_pattern = pattern;
#ifndef __serenity__
        if (regcomp(&m_pattern_regex, pattern.characters(), REG_EXTENDED)) {
            perror("regcomp");
        }
#endif
    }
    void set_max_length(i32 max_length) { m_max_length = max_length; }
    void set_min_length(i32 min_length) { m_min_length = min_length; }

    bool match_against_pattern(const String value) const;

private:
    virtual const char* class_name() const override { return "StringNode"; }

    Optional<u32> m_max_length;
    Optional<u32> m_min_length;
    Optional<String> m_pattern;

#ifndef __serenity__
    regex_t m_pattern_regex;
#endif
};

class NumberNode : public JsonSchemaNode {
public:
    NumberNode(JsonSchemaNode* parent, String id)
        : JsonSchemaNode(parent, id, InstanceType::Number)
    {
    }

    virtual void dump(int indent, String additional) const override;
    virtual bool validate(const JsonValue&, ValidationError&) const override;

    void set_minimum(float value)
    {
        m_minimum = value;
    }

    void set_maximum(float value)
    {
        m_maximum = value;
    }

private:
    virtual const char* class_name() const override { return "NumberNode"; }

    Optional<float> m_multiple_of;
    Optional<float> m_maximum;
    Optional<float> m_exclusive_maximum;
    Optional<float> m_minimum;
    Optional<float> m_exclusive_minimum;
};

class BooleanNode : public JsonSchemaNode {
public:
    BooleanNode(JsonSchemaNode* parent, String id, bool value)
        : JsonSchemaNode(parent, id, InstanceType::Boolean)
        , m_value(value)
    {
    }

    virtual void dump(int indent, String additional) const override;
    virtual bool validate(const JsonValue&, ValidationError&) const override;

private:
    virtual const char* class_name() const override { return "BooleanNode"; }

    bool m_value;
};

class NullNode : public JsonSchemaNode {
public:
    NullNode(JsonSchemaNode* parent, String id)
        : JsonSchemaNode(parent, id, InstanceType::Null)
    {
    }

    virtual void dump(int indent, String additional) const override;
    virtual bool validate(const JsonValue&, ValidationError&) const override;

private:
    virtual const char* class_name() const override { return "NullNode"; }
};

class UndefinedNode : public JsonSchemaNode {
public:
    UndefinedNode(JsonSchemaNode* parent)
        : JsonSchemaNode(parent, "", InstanceType::Undefined)
    {
    }

    virtual void dump(int indent, String additional) const override;
    virtual bool validate(const JsonValue&, ValidationError&) const override;

private:
    virtual const char* class_name() const override { return "UndefinedNode"; }
};

class ObjectNode : public JsonSchemaNode {
public:
    ObjectNode(JsonSchemaNode* parent)
        : JsonSchemaNode(parent, "", InstanceType::Object)
    {
    }

    ObjectNode(JsonSchemaNode* parent, String id)
        : JsonSchemaNode(parent, id, InstanceType::Object)
    {
    }

    void append_property(const String name, NonnullOwnPtr<JsonSchemaNode>&& node)
    {
        m_properties.set(name, move(node));
    }

    virtual void dump(int indent, String additional) const override;
    virtual bool validate(const JsonValue&, ValidationError&) const override;

    void append_required(String required)
    {
        m_required.append(required);
    }

    void set_additional_properties(bool additional_properties)
    {
        m_additional_properties = additional_properties;
    }

    HashMap<String, NonnullOwnPtr<JsonSchemaNode>>& properties() { return m_properties; }
    const HashMap<String, NonnullOwnPtr<JsonSchemaNode>>& properties() const { return m_properties; }
    const Vector<String>& required() const { return m_required; }

private:
    virtual const char* class_name() const override { return "ObjectNode"; }

    HashMap<String, NonnullOwnPtr<JsonSchemaNode>> m_properties;

    Optional<u32> m_max_properties;
    u32 m_min_properties = 0;
    Vector<String> m_required;
    bool m_additional_properties { true };
};

class ArrayNode : public JsonSchemaNode {
public:
    ArrayNode()
        : JsonSchemaNode("", InstanceType::Array)
    {
    }

    ArrayNode(JsonSchemaNode* parent)
        : JsonSchemaNode(parent, "", InstanceType::Array)
    {
    }

    ArrayNode(String id)
        : JsonSchemaNode(id, InstanceType::Array)
    {
    }

    ArrayNode(JsonSchemaNode* parent, String id)
        : JsonSchemaNode(parent, id, InstanceType::Array)
    {
    }

    ~ArrayNode()
    {
    }

    virtual void dump(int indent, String additional) const override;
    virtual bool validate(const JsonValue&, ValidationError&) const override;

    const NonnullOwnPtrVector<JsonSchemaNode>& items() { return m_items; }
    void append_item(NonnullOwnPtr<JsonSchemaNode>&& item) { m_items.append(move(item)); }

    bool unique_items() const { return m_unique_items; }
    void set_unique_items(bool unique_items) { m_unique_items = unique_items; }

    bool items_is_array() const { return m_items_is_array; }
    void set_items_is_array(bool items_is_array) { m_items_is_array = items_is_array; }

    const OwnPtr<JsonSchemaNode>& additional_items() const { return m_additional_items; }
    void set_additional_items(OwnPtr<JsonSchemaNode>&& additional_items) { m_additional_items = move(additional_items); }

private:
    virtual const char* class_name() const override { return "ArrayNode"; }

    NonnullOwnPtrVector<JsonSchemaNode> m_items;
    OwnPtr<JsonSchemaNode> m_additional_items;
    bool m_items_is_array { false };
    Optional<u32> m_max_items;
    u32 m_min_items = 0;
    bool m_unique_items { false };
};
}
