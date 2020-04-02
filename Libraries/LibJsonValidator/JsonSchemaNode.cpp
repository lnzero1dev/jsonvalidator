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

#include <AK/Function.h>
#include <AK/JsonObject.h>
#include <AK/JsonValue.h>
#include <LibJsonValidator/JsonSchemaNode.h>
#include <LibJsonValidator/Parser.h>
#include <LibJsonValidator/Validator.h>
#include <stdio.h>

namespace JsonValidator {

void JsonSchemaNode::resolve_reference(const JsonSchemaNode* root_node)
{
    if (!m_ref.is_empty())
        m_reference = resolve_reference(m_ref, root_node);

    if (m_not)
        m_not->resolve_reference(root_node);
    for (auto& item : m_defs)
        item.value->resolve_reference(root_node);
    for (auto& item : m_all_of)
        item.resolve_reference(root_node);
    for (auto& item : m_any_of)
        item.resolve_reference(root_node);
    for (auto& item : m_one_of)
        item.resolve_reference(root_node);
}

void ObjectNode::resolve_reference(const JsonSchemaNode* root_node)
{
    JsonSchemaNode::resolve_reference(root_node);
    for (auto& item : m_properties)
        item.value->resolve_reference(root_node);

    for (auto& item : m_pattern_properties)
        item.resolve_reference(root_node);
    for (auto& item : m_dependent_schemas)
        item.value->resolve_reference(root_node);
    if (m_additional_properties)
        m_additional_properties->resolve_reference(root_node);
}

void ArrayNode::resolve_reference(const JsonSchemaNode* root_node)
{
    JsonSchemaNode::resolve_reference(root_node);
    for (auto& item : m_items)
        item.resolve_reference(root_node);

    if (m_additional_items)
        m_additional_items->resolve_reference(root_node);
    if (m_contains)
        m_contains->resolve_reference(root_node);
}

int find_char(const String& haystack, const char& needle, const size_t start = 0)
{
    if (start > haystack.length())
        return -1;

    for (size_t i = start; i < haystack.length(); ++i) {
        auto ch = haystack[i];
        if (ch == needle) {
            return i;
        }
    }
    return -1;
}

const JsonSchemaNode* JsonSchemaNode::resolve_reference(const String& ref, const JsonSchemaNode* root_node) const
{
    if (ref.is_empty())
        return nullptr;

    String identifier;
    int last = 0, next = 0;
    const JsonSchemaNode* node = root_node;

    while (next < (int)ref.length()) {
        next = find_char(ref, '/', last);
        if (next < 0) {
            next = ref.length();
        }

        // prepare identifier
        identifier = ref.substring(last, next - last);
        identifier.replace("~1", "/", true);
        identifier.replace("~0", "~", true);

        node = node->resolve_reference_handle_identifer(identifier, root_node);
        last = next + 1;

        if (!node)
            return nullptr;
    }

    return node;
}

const JsonSchemaNode* JsonSchemaNode::resolve_reference_handle_identifer(const String& identifier, const JsonSchemaNode* root_node) const
{
    static bool selected_defs { false };

    if (identifier == "#" && is_root())
        return this;

    if (identifier.starts_with("#")) {
        // check list of anchors
        String copy = identifier;
        copy.replace("#", "");
        if (root_node->anchors().contains(copy))
            return root_node->anchors().get(copy).value();
    }

    if (identifier == "$defs") {
        selected_defs = true;
        return this;
    }

    if (selected_defs) {
        selected_defs = false;
        if (m_defs.contains(identifier))
            return m_defs.get(identifier).release_value();
        return nullptr;
    }

    if (m_id == identifier)
        return this;

    return nullptr;
}

const JsonSchemaNode* ObjectNode::resolve_reference_handle_identifer(const String& identifier, const JsonSchemaNode* root_node) const
{
    if (auto* ptr = JsonSchemaNode::resolve_reference_handle_identifer(identifier, root_node))
        return ptr;

    static bool selected_properties { false };

    if (identifier == "properties") {
        selected_properties = true;
        return this;
    }

    if (selected_properties) {
        selected_properties = false;
        if (m_properties.contains(identifier))
            return m_properties.get(identifier).release_value();
        return nullptr;
    }

    return nullptr;
}

const JsonSchemaNode* ArrayNode::resolve_reference_handle_identifer(const String& identifier, const JsonSchemaNode* root_node) const
{
    if (auto* ptr = JsonSchemaNode::resolve_reference_handle_identifer(identifier, root_node))
        return ptr;

    static bool selected_items { false };

    if (identifier == "items") {
        selected_items = true;
        return this;
    }

    if (selected_items) {
        selected_items = false;
        bool ok;
        u32 index = identifier.to_uint(ok);
        if (ok && index < m_items.size())
            return &m_items[index];
    }

    return nullptr;
}

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

void JsonSchemaNode::dump(int indent) const
{
    print_indent(indent);
    printf("%s (%s%s)", m_id.characters(), class_name(), m_required ? " *" : "");
    if (!m_ref.is_empty()) {
        auto ref = m_ref;
        ref.replace("~1", "/", true);
        ref.replace("~0", "~", true);
        printf("-> %s", ref.characters());
        if (m_reference)
            printf(" (resolved)");
    }
    printf("\n");

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

    if (m_one_of.size()) {
        print_indent(indent + 1);
        printf("oneOf:\n");
        for (auto& item : m_one_of) {
            item.dump(indent + 2);
        }
    }

    if (m_not) {
        print_indent(indent + 1);
        printf("not:\n");
        m_not->dump(indent + 2);
    }

    if (m_defs.size()) {
        print_indent(indent + 1);
        printf("$defs:\n");
        for (auto& item : m_defs) {
            print_indent(indent + 2);
            printf("%s:\n", item.key.characters());
            item.value->dump(indent + 3);
        }
    }
}

void ObjectNode::dump(int indent) const
{
    JsonSchemaNode::dump(indent);

    if (m_min_properties) {
        print_indent(indent + 1);
        printf("minProperties: %i\n", m_min_properties);
    }
    if (m_max_properties.has_value()) {
        print_indent(indent + 1);
        printf("maxProperties: %i\n", m_max_properties.value());
    }

    for (auto& property : m_properties) {
        print_indent(indent + 1);
        printf("%s:\n", property.key.characters());
        property.value->dump(indent + 1);
    }
    for (auto& property : m_pattern_properties) {
        print_indent(indent + 1);
        printf("%s:\n", property.pattern().characters());
        property.dump(indent + 1);
    }
    if (m_additional_properties) {
        print_indent(indent + 1);
        printf("additionalProperties:\n");
        m_additional_properties->dump(indent + 1);
    }
    if (m_dependent_schemas.size()) {
        print_indent(indent + 1);
        printf("dependentSchemas:\n");
        for (auto& dependent_schema : m_dependent_schemas) {
            print_indent(indent + 2);
            printf("%s:\n", dependent_schema.key.characters());
            dependent_schema.value->dump(indent + 2);
        }
    }
}

void ArrayNode::dump(int indent) const
{
    JsonSchemaNode::dump(indent);

    if (m_items.size())
        for (auto& item : m_items)
            item.dump(indent + 1);
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
    calculate_json_pointer();

    // check if type is matching
    if (!m_type_str.is_empty() && !validate_type(m_type, json)) {
        e.addf("type validation failed: have '%s', but looking for node with type '%s'", json.to_string().characters(), to_string(m_type).characters());
        return false;
    }

    // check if required is matching
    if (m_required && json.is_undefined()) {
        e.addf("item is required, but is not present at %s, %s", json_pointer().characters(), json.to_string().characters());
        return false;
    }

    // run all checks of "allOf" on this node
    bool valid = true;

    for (auto& item : m_all_of)
        valid &= item.validate(json, e);

    if (m_reference)
        valid &= m_reference->validate(json, e);

    // run all checks of "anyOf" on this node. Valid if one of the any is true.
    bool any = true;
    if (m_any_of.size()) {
        any = false;
        ValidationError any_of_errors;
        for (auto& item : m_any_of) {
            any |= item.validate(json, any_of_errors);
        }
        if (!any)
            e.addf("not item matched in anyOf at %s, %s", json_pointer().characters(), json.to_string().characters());
    }

    if (m_not) {
        ValidationError not_errors;
        auto item_valid = !(m_not->validate(json, not_errors));
        valid &= item_valid;
        if (!item_valid)
            e.append(not_errors);
    }

    bool one = true;
    if (m_one_of.size()) {
        one = false;
        ValidationError one_of_errors;
        for (auto& item : m_one_of) {
            bool this_one = item.validate(json, one_of_errors);
            if (!one && this_one)
                one = true;
            else if (one && this_one) {
                one = false;
                break;
            }
        }
        if (!one)
            e.addf("not one item matched in oneOf at %s, %s", json_pointer().characters(), json.to_string().characters());
    }

    bool enum_matched = true;
    if (m_enum_items.size()) {
        enum_matched = false;
        for (auto& item : m_enum_items) {
            enum_matched |= item.equals(json);
        }
        if (!enum_matched)
            e.addf("No enum matched at %s, %s", json_pointer().characters(), json.to_string().characters());
    }

    if (json.is_object()) {
        // check for definitions in values.
        // FIXME: Unclear why this is even in the tests... what's the use case for values to have $defs?
        Parser p;
        if (!p.parse_sub_schema("$defs", json.as_object(), nullptr, [](auto&, auto&&) {})) {
            valid = false;
            e.addf("Subschema in $defs not valid at %s, %s", json_pointer().characters(), json.to_string().characters());
        }
    }

    return valid & any & one & enum_matched;
}

bool StringNode::validate(const JsonValue& json, ValidationError& e) const
{
    bool valid = JsonSchemaNode::validate(json, e);

    if (!json.is_string())
        return valid;

    auto value = json.as_string();

    if (m_pattern.has_value()) {
        auto item_valid = match_against_pattern(value);
        valid &= item_valid;
        if (!item_valid)
            e.addf("String pattern not matching %s, %s", json_pointer().characters(), json.to_string().characters());
    }

    if (m_max_length.has_value()) {
        auto item_valid = !(value.length() > m_max_length.value());
        valid &= item_valid;
        if (!item_valid)
            e.addf("maxLenght violation at %s, %s", json_pointer().characters(), json.to_string().characters());
    }

    if (m_min_length.has_value()) {
        auto item_valid = !(value.length() < m_min_length.value());
        valid &= item_valid;
        if (!item_valid)
            e.addf("minLenght violation at %s, %s", json_pointer().characters(), json.to_string().characters());
    }

    return valid;
}

bool NumberNode::validate(const JsonValue& json, ValidationError& e) const
{
    bool valid = JsonSchemaNode::validate(json, e);

    if (!json.is_number())
        return valid;

    if (type_str() == "integer" && !(json.is_i32() || json.is_i64() || json.is_u32() || json.is_u64())) {
        e.addf("Number is not an integer value at %s, %s", json_pointer().characters(), json.to_string().characters());
        valid = false;
    }

    if (m_minimum.has_value()) {
        if (json.to_number<double>() < m_minimum.value()) {
            e.addf("Minimum invalid: value is %f, allowed is: %f at %s, %s",
                json.to_number<double>(), m_minimum.value(), json_pointer().characters(), json.to_string().characters());
            valid = false;
        }
    }

    if (m_maximum.has_value()) {
        if (json.to_number<double>() > m_maximum.value()) {
            e.addf("Maximum invalid: value is %f, allowed is: %f at %s, %s",
                json.to_number<double>(), m_maximum.value(), json_pointer().characters(), json.to_string().characters());
            valid = false;
        }
    }

    if (m_exclusive_minimum.has_value()) {
        if (json.to_number<double>() <= m_exclusive_minimum.value()) {
            e.addf("exclusiveMinimum invalid: value is %f, allowed is: %f at %s, %s",
                json.to_number<double>(), m_exclusive_minimum.value(), json_pointer().characters(), json.to_string().characters());
            valid = false;
        }
    }

    if (m_exclusive_maximum.has_value()) {
        if (json.to_number<double>() >= m_exclusive_maximum.value()) {
            e.addf("exclusiveMaximum invalid: value is %f, allowed is: %f at %s, %s",
                json.to_number<double>(), m_exclusive_maximum.value(), json_pointer().characters(), json.to_string().characters());
            valid = false;
        }
    }

    if (m_multiple_of.has_value()) {
        double result = json.to_number<double>() / m_multiple_of.value();
        if ((result - (u64)result) != 0) {
            e.addf("multipleOf invalid: value is %f, allowed is multipleOf: %f at %s, %s",
                json.to_number<double>(), m_multiple_of.value(), json_pointer().characters(), json.to_string().characters());
            valid = false;
        }
    }

    return valid;
}

bool BooleanNode::validate(const JsonValue& json, ValidationError&) const
{
    if (m_value.has_value())
        return m_value.value();

    return json.is_bool();
}

bool ObjectNode::validate(const JsonValue& json, ValidationError& e) const
{
    bool valid = JsonSchemaNode::validate(json, e);

    if (!json.is_object())
        return valid;

#ifdef JSON_SCHEMA_DEBUG
    printf("Validating %lu properties.\n", m_properties.size());
#endif

    if (m_min_properties) {
        if (json.as_object().size() < (int)m_min_properties) {
            e.addf("minProperties value of %i not met with %i items at %s, %s",
                m_min_properties, json.as_object().size(), json_pointer().characters(), json.to_string().characters());
            valid = false;
        }
    }

    if (m_max_properties.has_value())
        if (json.as_object().size() > (int)m_max_properties.value()) {
            e.addf("maxProperties value of %i not met with %i items at %s, %s",
                m_max_properties.value(), json.as_object().size(), json_pointer().characters(), json.to_string().characters());
            valid = false;
        }

    // check for missing items
    for (auto& required : m_required) {
        if (!json.as_object().has(required)) {
            e.addf("required value %s not found at %s, %s", required.characters(), json_pointer().characters(), json.to_string().characters());
            valid = false;
        }
    }

    // check for depentent required
    for (auto& required : m_dependent_required) {
        if (json.as_object().has(required.key)) {
            for (auto& dependency : required.value) {
                if (!json.as_object().has(dependency)) {
                    e.addf("dependentRequired dependency %s not found at %s, %s", dependency.characters(), json_pointer().characters(), json.to_string().characters());
                    valid = false;
                }
            }
        }
    }

    Vector<const JsonSchemaNode*> dependent_schemas_to_apply;
    for (auto& dependent_schema : m_dependent_schemas) {
        if (json.as_object().has(dependent_schema.key)) {
            auto item_valid = dependent_schema.value->validate(json.as_object(), e);
            valid &= item_valid;
            if (!item_valid)
                e.addf("dependentSchema not valid at %s, %s", json_pointer().characters(), json.to_string().characters());
        }
    }

    json.as_object().for_each_member([&](auto& key, auto& value) {
        // key is in properties
        if (m_properties.contains(key)) {
            auto* property = m_properties.get(key).value();
            valid &= property->validate(value, e);

        } else {
            // check all pattern properties for a match
            bool match = false;
            for (auto& pattern_property : m_pattern_properties) {
                if (pattern_property.match_against_pattern(key)) {
                    match = true;
                    valid &= pattern_property.validate(value, e);
                }
            }

            // it's time to check against additionalProperties, if available
            if (!match) {
                if (m_additional_properties) {
                    auto item_valid = m_additional_properties->validate(value, e);
                    valid &= item_valid;
                    if (!item_valid)
                        e.addf("additionalProperty not valid at %s, %s", json_pointer().characters(), json.to_string().characters());

                } else {
                    e.addf("property %s not in schema definition at %s, %s", key.characters(), json_pointer().characters(), json.to_string().characters());
                    valid = false;
                }
            }
        }

        if (m_property_names) {
            auto item_valid = m_property_names->validate(JsonValue(key), e);
            valid &= item_valid;
            if (!item_valid)
                e.addf("propertyNames not valid at %s, %s", json_pointer().characters(), json.to_string().characters());
        }
    });

    return valid;
}

bool ArrayNode::validate(const JsonValue& json, ValidationError& e) const
{
    bool valid = JsonSchemaNode::validate(json, e);

    if (!json.is_array())
        return valid;

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
    bool contains_valid { false };

    ValidationError contains_error;
    for (size_t i = 0; i < values.size(); ++i) {
        auto& value = values[i];

        // check for duplicate hash of array item
        if (m_unique_items) {
            auto hash = value.to_string().impl()->hash();
            if (hashes.get(hash).has_value()) {
                e.addf("uniqueItems violation with duplicate item %s at %s, %s", value.to_string().characters(), json_pointer().characters(), json.to_string().characters());
                valid = false;
            }
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

        if (m_contains && !contains_valid)
            contains_valid = m_contains->validate(value, contains_error);
    }

    if (m_contains) {
        valid &= contains_valid;
        if (!contains_valid)
            e.addf("Array contains violation at %s, %s", json_pointer().characters(), json.to_string().characters());
    }

    return valid;
}

String JsonSchemaNode::calculate_json_pointer() const
{

    if (!parent())
        return "#";

    StringBuilder b;
    Optional<String> member_name;
    Optional<u32> index;

    b.append(parent()->calculate_json_pointer());
    b.append("/");

    if (parent()->is_object()) {
        if (m_identified_by_pattern) {
            b.append("patternProperties");
            // FIXME: how can we retrieve the current matched value from the actual json value? (it is not possible to store in tree....)
        } else {
            auto& parent_object = *static_cast<const ObjectNode*>(parent());
            for (auto& item : parent_object.properties()) {
                if (item.value.ptr() == this) {
                    b.append("properties/");
                    b.append(item.key);
                    break;
                }
            }
        }

    } else if (parent()->is_array()) {
        // search all items and get the index
        auto& parent_array = *static_cast<const ArrayNode*>(parent());
        if (parent_array.contains() == this)
            b.append("contains");
        else if (parent_array.additional_items() == this)
            b.append("additionalItems");
        else if (parent_array.items().size()) {
            size_t idx = 0;
            for (auto& item : parent_array.items()) {
                if (&item == this) {
                    b.append("items/");
                    b.append(idx);
                }
                ++idx;
            }
        }
    }

    {
        // search any_of, all_of, ...
        size_t idx = 0;
        for (auto& item : parent()->any_of()) {
            if (&item == this) {
                b.append("anyOf/");
                b.append(idx);
            }
            ++idx;
        }

        idx = 0;
        for (auto& item : parent()->all_of()) {
            if (&item == this) {
                b.append("allOf/");
                b.append(idx);
            }
            ++idx;
        }

        idx = 0;
        for (auto& item : parent()->one_of()) {
            if (&item == this) {
                b.append("oneOf/");
                b.append(idx);
            }
            ++idx;
        }

        if (parent()->get_not() == this)
            b.append("not");

        for (auto& item : parent()->defs()) {
            if (item.value == this) {
                b.append("$defs/");
                b.append(item.key);
            }
        }
    }

    return b.build();
}

bool StringNode::match_against_pattern(const String& value) const
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
}
