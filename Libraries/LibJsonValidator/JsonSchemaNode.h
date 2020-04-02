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
#include <AK/Badge.h>
#include <AK/HashMap.h>
#include <AK/HashTable.h>
#include <AK/JsonValue.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/NonnullOwnPtrVector.h>
#include <AK/OwnPtr.h>
#include <AK/String.h>
#include <cstdio>

#ifndef __serenity__
#    include <regex.h>
#endif

namespace JsonValidator {

String replace(const String& haystack, const String& needle, const String& replacement);

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

    void set_default_value(JsonValue default_value) { m_default_value = default_value; }
    void set_id(String id) { m_id = id; }
    void set_type(InstanceType type) { m_type = type; }
    void set_type_str(const String& type_str) { m_type_str = type_str; }
    void set_required(bool required) { m_required = required; }

    void append_all_of(NonnullOwnPtr<JsonSchemaNode>&& node) { m_all_of.append(move(node)); }
    void append_any_of(NonnullOwnPtr<JsonSchemaNode>&& node) { m_any_of.append(move(node)); }
    void append_one_of(NonnullOwnPtr<JsonSchemaNode>&& node) { m_one_of.append(move(node)); }
    void append_defs(const String& key, NonnullOwnPtr<JsonSchemaNode>&& node) { m_defs.set(key, move(node)); }
    void set_not(NonnullOwnPtr<JsonSchemaNode>&& node) { m_not = move(node); }
    void set_anchors(HashMap<String, JsonSchemaNode*>&& anchors) { m_anchors = move(anchors); }

    bool append_enum_item(JsonValue enum_item)
    {
        for (auto& item : m_enum_items) {
            if (enum_item.equals(item))
                return false;
        }

        m_enum_items.append(enum_item);
        return true;
    }

    void compile_pattern(const String pattern)
    {
        m_pattern = pattern;
#ifndef __serenity__
        if (regcomp(&m_pattern_regex, pattern.characters(), REG_EXTENDED)) {
            perror("regcomp");
        }
#endif
    }


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
    const String& type_str() const { return m_type_str; }
    const String& id() const { return m_id; }
    JsonValue default_value() const { return m_default_value; }
    const Vector<JsonValue>& enum_items() const { return m_enum_items; }
    const String& pattern() const { return m_pattern; }
    const HashMap<String, JsonSchemaNode*>& anchors() { return m_anchors; }

    JsonSchemaNode* parent() { return m_parent; }
    const JsonSchemaNode* parent() const { return m_parent; }

    JsonSchemaNode* reference() { return m_reference; }
    const JsonSchemaNode* reference() const { return m_reference; }

    void set_ref(const String& ref)
    {
        m_ref = ref;
#ifndef __serenity__
        if (m_ref.contains("%")) {
            for (u8 i = 0; i < 255; ++i) {
                StringBuilder b;
                b.append("%");
                if (i < 16)
                    b.appendf("(0%x|0%X)", i, i);
                else
                    b.appendf("(%x|%X)", i, i);
                StringBuilder c;

                if (i == 47) // 0x2F = '/'
                    c.append("~1");
                else if (i == 126) // 0x7E = '~'
                    c.append("~0");
                else
                    c.append(char(i));

                m_ref = replace(m_ref, b.build(), c.build());
            }
        }
#endif
    }
    void set_root(Badge<Parser>) { m_root = true; }

    String json_pointer() const;

    virtual bool is_object() const { return false; }
    virtual bool is_array() const { return false; }
    virtual bool is_null() const { return false; }
    virtual bool is_undefined() const { return false; }
    virtual bool is_number() const { return false; }
    virtual bool is_boolean() const { return false; }
    virtual bool is_string() const { return false; }
    bool is_root() const { return m_root; }

    virtual void resolve_reference(JsonSchemaNode* root_node);
    virtual JsonSchemaNode* resolve_reference_handle_identifer(const String& identifier, JsonSchemaNode* root_node);

    JsonSchemaNode* resolve_reference(const String& ref, JsonSchemaNode* node);

protected:
    JsonSchemaNode() {}

    JsonSchemaNode(String id, InstanceType type)
        : m_id(move(id))
        , m_type(type)
    {
    }

    JsonSchemaNode(JsonSchemaNode* parent, String id, InstanceType type)
        : m_id(move(id))
        , m_type(type)
        , m_parent(parent)
    {
    }

private:
    String m_id;
    InstanceType m_type;
    String m_type_str;
    JsonValue m_default_value;
    Vector<JsonValue> m_enum_items;
    bool m_identified_by_pattern { false };
    bool m_root { false };
    String m_pattern;
#ifndef __serenity__
    regex_t m_pattern_regex;
#endif
    JsonSchemaNode* m_parent { nullptr };
    JsonSchemaNode* m_reference { nullptr };
    String m_ref;
    bool m_required { false };

    NonnullOwnPtrVector<JsonSchemaNode> m_all_of;
    NonnullOwnPtrVector<JsonSchemaNode> m_any_of;
    NonnullOwnPtrVector<JsonSchemaNode> m_one_of;
    OwnPtr<JsonSchemaNode> m_not;
    HashMap<String, NonnullOwnPtr<JsonSchemaNode>> m_defs;
    HashMap<String, JsonSchemaNode*> m_anchors;
};

class StringNode : public JsonSchemaNode {
public:
    StringNode(String id)
        : JsonSchemaNode(id, InstanceType::String)
    {
    }

    StringNode(JsonSchemaNode* parent, String id)
        : JsonSchemaNode(parent, id, InstanceType::String)
    {
    }

    virtual void dump(int indent, String additional) const override;
    virtual bool validate(const JsonValue&, ValidationError&) const override;
    virtual bool is_string() const override { return true; }

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
    NumberNode(String id)
        : JsonSchemaNode(id, InstanceType::Number)
    {
    }

    NumberNode(JsonSchemaNode* parent, String id)
        : JsonSchemaNode(parent, id, InstanceType::Number)
    {
    }

    virtual void dump(int indent, String additional) const override;
    virtual bool validate(const JsonValue&, ValidationError&) const override;
    virtual bool is_number() const override { return true; }

    void set_minimum(double value) { m_minimum = value; }
    void set_maximum(double value) { m_maximum = value; }
    void set_exclusive_minimum(double value) { m_exclusive_minimum = value; }
    void set_exclusive_maximum(double value) { m_exclusive_maximum = value; }
    void set_multiple_of(double value) { m_multiple_of = value; }

private:
    virtual const char* class_name() const override { return "NumberNode"; }

    Optional<double> m_multiple_of;
    Optional<double> m_maximum;
    Optional<double> m_exclusive_maximum;
    Optional<double> m_minimum;
    Optional<double> m_exclusive_minimum;
};

class BooleanNode : public JsonSchemaNode {
public:
    BooleanNode(String id, bool value)
        : JsonSchemaNode(id, InstanceType::Boolean)
        , m_value(value)
    {
    }

    BooleanNode(JsonSchemaNode* parent, String id)
        : JsonSchemaNode(parent, id, InstanceType::Boolean)
    {
    }

    BooleanNode(JsonSchemaNode* parent, String id, bool value)
        : JsonSchemaNode(parent, id, InstanceType::Boolean)
        , m_value(value)
    {
    }

    virtual void dump(int indent, String additional) const override;
    virtual bool validate(const JsonValue&, ValidationError&) const override;
    virtual bool is_boolean() const override { return true; }

private:
    virtual const char* class_name() const override { return "BooleanNode"; }

    Optional<bool> m_value;
};

class NullNode : public JsonSchemaNode {
public:
    NullNode(String id)
        : JsonSchemaNode(id, InstanceType::Null)
    {
    }

    NullNode(JsonSchemaNode* parent, String id)
        : JsonSchemaNode(parent, id, InstanceType::Null)
    {
    }

    virtual void dump(int indent, String additional) const override;
    virtual bool validate(const JsonValue&, ValidationError&) const override;
    virtual bool is_null() const override { return true; }

private:
    virtual const char* class_name() const override { return "NullNode"; }
};

class UndefinedNode : public JsonSchemaNode {
public:
    UndefinedNode()
        : JsonSchemaNode("", InstanceType::Undefined)
    {
    }

    UndefinedNode(JsonSchemaNode* parent)
        : JsonSchemaNode(parent, "", InstanceType::Undefined)
    {
    }

    virtual void dump(int indent, String additional) const override;
    virtual bool validate(const JsonValue&, ValidationError&) const override;
    virtual bool is_undefined() const override { return true; }

private:
    virtual const char* class_name() const override { return "UndefinedNode"; }
};

class ObjectNode : public JsonSchemaNode {
public:
    ObjectNode(String id)
        : JsonSchemaNode(id, InstanceType::Object)
    {
    }

    ObjectNode(JsonSchemaNode* parent, String id)
        : JsonSchemaNode(parent, id, InstanceType::Object)
    {
    }

    virtual void dump(int indent, String additional) const override;
    virtual bool validate(const JsonValue&, ValidationError&) const override;
    virtual bool is_object() const override { return true; }
    virtual void resolve_reference(JsonSchemaNode* root_node) override;
    virtual JsonSchemaNode* resolve_reference_handle_identifer(const String& identifier, JsonSchemaNode* root_node) override;

    void append_property(const String name, NonnullOwnPtr<JsonSchemaNode>&& node)
    {
        m_properties.set(name, move(node));
    }

    void append_required(String required)
    {
        m_required.set(required);
    }

    void append_dependent_required(String dependant, HashTable<String> dependencies)
    {
        m_dependent_required.set(dependant, dependencies);
    }

    void append_dependent_schema(String dependant, NonnullOwnPtr<JsonSchemaNode>&& node)
    {
        m_dependent_schemas.set(dependant, move(node));
    }

    void append_pattern_property(NonnullOwnPtr<JsonSchemaNode>&& node)
    {
        m_pattern_properties.append(move(node));
    }

    void set_additional_properties(NonnullOwnPtr<JsonSchemaNode>&& node)
    {
        m_additional_properties = move(node);
    }

    void set_property_names(NonnullOwnPtr<JsonSchemaNode>&& node)
    {
        m_property_names = move(node);
    }

    void set_max_properties(u32 max_properties)
    {
        m_max_properties = max_properties;
    }

    void set_min_properties(u32 min_properties)
    {
        m_min_properties = min_properties;
    }

    HashMap<String, NonnullOwnPtr<JsonSchemaNode>>& properties() { return m_properties; }
    const HashMap<String, NonnullOwnPtr<JsonSchemaNode>>& properties() const { return m_properties; }
    const HashTable<String>& required() const { return m_required; }
    const HashMap<String, HashTable<String>>& dependent_required() const { return m_dependent_required; }

private:
    virtual const char* class_name() const override { return "ObjectNode"; }

    HashMap<String, NonnullOwnPtr<JsonSchemaNode>> m_properties;
    NonnullOwnPtrVector<JsonSchemaNode> m_pattern_properties;

    Optional<u32> m_max_properties;
    u32 m_min_properties = 0;
    HashTable<String> m_required;
    HashMap<String, HashTable<String>> m_dependent_required;
    HashMap<String, NonnullOwnPtr<JsonSchemaNode>> m_dependent_schemas;
    OwnPtr<JsonSchemaNode> m_additional_properties = make<BooleanNode>(this, "", true);
    OwnPtr<JsonSchemaNode> m_property_names;
};

class ArrayNode : public JsonSchemaNode {
public:
    ArrayNode(String id)
        : JsonSchemaNode(id, InstanceType::Array)
    {
    }

    ArrayNode(JsonSchemaNode* parent, String id)
        : JsonSchemaNode(parent, id, InstanceType::Array)
    {
    }

    virtual void dump(int indent, String additional) const override;
    virtual bool validate(const JsonValue&, ValidationError&) const override;
    virtual bool is_array() const override { return true; }
    virtual void resolve_reference(JsonSchemaNode* root_node) override;
    virtual JsonSchemaNode* resolve_reference_handle_identifer(const String& identifier, JsonSchemaNode* root_node) override;

    const NonnullOwnPtrVector<JsonSchemaNode>& items() { return m_items; }
    void append_item(NonnullOwnPtr<JsonSchemaNode>&& item) { m_items.append(move(item)); }

    bool unique_items() const { return m_unique_items; }
    void set_unique_items(bool unique_items) { m_unique_items = unique_items; }

    bool items_is_array() const { return m_items_is_array; }
    void set_items_is_array(bool items_is_array) { m_items_is_array = items_is_array; }

    const OwnPtr<JsonSchemaNode>& additional_items() const { return m_additional_items; }
    void set_additional_items(OwnPtr<JsonSchemaNode>&& additional_items) { m_additional_items = move(additional_items); }

    const OwnPtr<JsonSchemaNode>& contains() const { return m_contains; }
    void set_contains(OwnPtr<JsonSchemaNode>&& contains) { m_contains = move(contains); }

    void set_max_items(u32 value) { m_max_items = value; }
    void set_min_items(u32 value) { m_min_items = value; }

private:
    virtual const char* class_name() const override { return "ArrayNode"; }

    NonnullOwnPtrVector<JsonSchemaNode> m_items;
    OwnPtr<JsonSchemaNode> m_contains;
    OwnPtr<JsonSchemaNode> m_additional_items;
    bool m_items_is_array { false };
    Optional<u32> m_max_items;
    u32 m_min_items = 0;
    bool m_unique_items { false };
};
}
