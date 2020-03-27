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

#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <LibJsonValidator/JsonSchemaNode.h>
#include <LibJsonValidator/Parser.h>
#include <LibJsonValidator/Validator.h>
#include <stdio.h>

namespace JsonValidator {

JsonSchemaNode::~JsonSchemaNode() {}

static void print_indent(int indent)
{
    for (int i = 0; i < indent * 2; ++i)
        putchar(' ');
}

String to_string(InstanceType type)
{
    switch (type) {
    case InstanceType::Object:
        return "object";
    case InstanceType::Array:
        return "array";
    case InstanceType::String:
        return "string";
    case InstanceType::Number:
        return "number";
    case InstanceType::Boolean:
        return "boolean";
    case InstanceType::Undefined:
        return "undefined";
    case InstanceType::Null:
        return "null";
    }
    return "";
}

void JsonSchemaNode::dump(int indent, String additional) const
{
    print_indent(indent);
    printf("%s (%s%s%s)\n", m_id.characters(), class_name(), m_required ? " *" : "", additional.characters());

    if (m_all_of.size()) {
        print_indent(indent + 1);
        printf("allOf:\n");
        for (auto& item : m_all_of) {
            item.dump(indent + 2);
        }
    }

    if (m_any_of.size()) {
        print_indent(indent + 1);
        printf("anyOf:\n");
        for (auto& item : m_any_of) {
            item.dump(indent + 2);
        }
    }
}

void ObjectNode::dump(int indent, String = "") const
{
    JsonSchemaNode::dump(indent);
    for (auto& property : properties()) {
        print_indent(indent + 1);
        printf("%s:\n", property.key.characters());
        property.value->dump(indent + 1);
    }
}

void ArrayNode::dump(int indent, String = "") const
{
    JsonSchemaNode::dump(indent, m_unique_items ? " with unique_items" : "");

    if (m_items.size())
        for (auto& item : m_items)
            item.dump(indent + 1);
}

void StringNode::dump(int indent, String = "") const
{
    JsonSchemaNode::dump(indent);
}

void NumberNode::dump(int indent, String = "") const
{
    JsonSchemaNode::dump(indent);
}

void BooleanNode::dump(int indent, String = "") const
{
    JsonSchemaNode::dump(indent);
}

void NullNode::dump(int indent, String = "") const
{
    JsonSchemaNode::dump(indent);
}

void UndefinedNode::dump(int indent, String = "") const
{
    JsonSchemaNode::dump(indent);
}

bool validate_type(InstanceType type, const JsonValue& json)
{
    if (type == InstanceType::Array && json.is_array())
        return true;

    else if (type == InstanceType::Object && json.is_object())
        return true;

    else if (type == InstanceType::String && json.is_string())
        return true;

    else if (type == InstanceType::Number && json.is_number())
        return true;

    else if (type == InstanceType::Null && json.is_null())
        return true;

    else if (type == InstanceType::Boolean)
        // boolean type matches always! validation checks for true/false of boolean value.
        return true;

    else if (type == InstanceType::Undefined)
        // we don't know the type and assume it's ok
        return true;

    return false;
}

bool JsonSchemaNode::validate(const JsonValue& json, ValidationError& e) const
{
#ifdef JSON_SCHEMA_DEBUG
    printf("Validating node: %s (%s)\n", m_id.characters(), class_name());
#endif

    // check if type is matching
    if (!validate_type(m_type, json)) {
        e.addf("type validation failed: have '%s', but looking for node with type '%s'", json.to_string().characters(), to_string(m_type).characters());
        return false;
    }

    // check if required is matching
    if (m_required && json.is_undefined()) {
        e.addf("item %s is required, but is not present (json: %s)", path().characters(), json.to_string().characters());
        return false;
    }

    // run all checks of "allOf" on this node
    bool valid = true;
    for (auto& item : m_all_of) {
        valid &= item.validate(json, e);
    }

    // run all checks of "anyOf" on this node. Valid if one of the any is true.
    bool any = true;
    if (m_any_of.size()) {
        any = false;
        for (auto& item : m_any_of) {
            any |= item.validate(json, e);
        }
    }
    return valid & any;
}

bool StringNode::validate(const JsonValue& json, ValidationError& e) const
{
    bool valid = JsonSchemaNode::validate(json, e);

    if (json.is_string()) {
        auto value = json.as_string();

        if (m_pattern.has_value()) {
            valid &= match_against_pattern(value);
        }

        if (m_max_length.has_value()) {
            valid &= !(value.length() > m_max_length.value());
        }

        if (m_min_length.has_value()) {
            valid &= !(value.length() <= m_min_length.value());
        }
    } else if (m_pattern.has_value()) {
        return true; // non-strings are ignored for pattern!
    }

    return valid;
}

bool StringNode::match_against_pattern(const String value) const
{
#ifdef __serenity__
    UNUSED_PARAM(value);
    if (m_pattern.has_value()) {
        if (m_pattern.value() == "^.*$") {
            // FIXME: Match everything, to be replaced with below code from else case when
            // posix pattern matching implemented
            return true;
        }
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

bool NumberNode::validate(const JsonValue& json, ValidationError& e) const
{
    bool valid = JsonSchemaNode::validate(json, e);

    if (m_is_integer && !(json.is_i32() || json.is_i64() || json.is_u32() || json.is_u64()))
        valid = false;

    if (m_minimum.has_value()) {
        if (json.to_number<float>() <= m_minimum.value()) {
            e.addf("Minimum invalid: value is %f, allowed is: %f", json.to_number<float>(), m_minimum.value());
            valid = false;
        }
    }

    if (m_maximum.has_value()) {
        if (json.to_number<float>() >= m_maximum.value()) {
            e.addf("Maximum invalid: value is %f, allowed is: %f", json.to_number<float>(), m_maximum.value());
            valid = false;
        }
    }

    return valid;
}

bool BooleanNode::validate(const JsonValue& json, ValidationError& e) const
{
    bool valid = JsonSchemaNode::validate(json, e);

    return valid & m_value;
}

bool NullNode::validate(const JsonValue& json, ValidationError& e) const
{
    return JsonSchemaNode::validate(json, e);
}

bool UndefinedNode::validate(const JsonValue& json, ValidationError& e) const
{
    return JsonSchemaNode::validate(json, e);
}

String JsonSchemaNode::path() const
{
    StringBuilder b;
    String member_name;
    if (parent()) {
        b.append(parent()->path());
        if (parent()->type() == InstanceType::Object) {
            for (auto& item : static_cast<ObjectNode*>(parent())->properties()) {
                if (item.value.ptr() == this) {
                    member_name = item.key;
                }
            }
        }
    }
    b.appendf("/%s", (!id().is_empty() ? id() : to_string(type())).characters());
    if (!member_name.is_empty()) {
        b.appendf("[%s]", member_name.characters());
    }
    return b.build();
}

bool ObjectNode::validate(const JsonValue& json, ValidationError& e) const
{
    bool valid = JsonSchemaNode::validate(json, e);

    if (!json.is_object()) {
        e.addf("json is not object: %s", json.to_string().characters());
        return false;
    }

#ifdef JSON_SCHEMA_DEBUG
    printf("Validating %lu properties.\n", m_properties.size());
#endif

    Vector<String> json_property_keys;
    size_t members_count = 0;
    json.as_object().for_each_member([&](auto& key, auto&) {
        json_property_keys.append(key);
        ++members_count;
    });

    for (auto& property : m_properties) {
        if (property.value->identified_by_pattern()) {
            // get all property keys that match the pattern
            Vector<String> matched;
            for (auto& key : json_property_keys)
                if (property.value->match_against_pattern(key))
                    matched.append(key);

            Optional<size_t> n;
            for (auto& match : matched)
                if ((n = json_property_keys.find_first_index(match)).has_value())
                    json_property_keys.remove(n.value());

            StringBuilder keys_builder;
            keys_builder.join<String, Vector<String>>(", ", matched);
#ifdef JSON_SCHEMA_DEBUG
            printf("%lu/%lu key(s) matched the pattern: %s\n", matched.size(), members_count, keys_builder.build().characters());
#endif
            if (matched.size()) {
                for (auto& match : matched) {
                    valid &= property.value->validate(json.as_object().get(match), e);
                }
            }

        } else if (json.as_object().has(property.key)) {
#ifdef JSON_SCHEMA_DEBUG
            printf("Validating property %s.\n", property.key.characters());
#endif
            valid &= property.value->validate(json.as_object().get(property.key), e);

            auto index = json_property_keys.find_first_index(property.key);
            if (index.has_value())
                json_property_keys.remove(index.value());
#ifdef JSON_SCHEMA_DEBUG
            else
                printf("error: could not remove key from vector\n");
#endif
        } else if (property.value->required()) {
            e.addf("required value %s not found at %s", property.key.characters(), property.value->path().characters());
            return false;
        }
    }

    // when no additional properties allowed, json_property_keys must be empty
    if (!m_additional_properties && json_property_keys.size()) {
        StringBuilder props_builder;
        props_builder.append("found additional properties \"");
        props_builder.join<String, Vector<String>>(", ", json_property_keys);

        props_builder.append("\", but not allowed due to additionalProperties");
        e.add(props_builder.build());
        return false;
    }

    return valid;
}

bool ArrayNode::validate(const JsonValue& json, ValidationError& e) const
{
    bool valid = JsonSchemaNode::validate(json, e);

    if (!json.is_array())
        return true; // ignore non array types...

    auto& values = json.as_array().values();

    // validate min and max items
    if (values.size() < m_min_items) {
        e.add("minItems violation");
        return false;
    }
    if (m_max_items.has_value() && values.size() > m_max_items.value()) {
        e.add("maxItems violation");
        return false;
    }

    // validate each json array element against the items spec
    HashMap<u32, bool> hashes;
    for (size_t i = 0; i < values.size(); ++i) {
        auto& value = values[i];

        // check for duplicate hash of array item
        if (m_unique_items) {
            auto hash = value.to_string().impl()->hash();
            if (hashes.get(hash).has_value()) {
                e.add("duplicate item found, but not allowed due to uniqueItems");
                return false;
            } else
                hashes.set(hash, true);
        }

        if (m_items_is_array) {
            if (m_items.size() > i) {
                valid &= m_items.at(i).validate(value, e);
            } else {
                if (m_additional_items)
                    valid &= m_additional_items->validate(value, e);
            }
        } else {
            if (m_items.size()) {
                valid &= m_items.at(0).validate(value, e);
            }
        }
    }

    return valid;
}

}
