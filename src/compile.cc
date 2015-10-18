#include "./compile.h"
#include "tree_sitter/compiler.h"
#include <utility>
#include <vector>

namespace node_tree_sitter_compiler {

using namespace v8;
using namespace tree_sitter;
using std::string;
using std::get;
using std::pair;
using std::vector;

static std::string StringFromJsString(Handle<String> js_string) {
  String::Utf8Value utf8_string(js_string);
  return std::string(*utf8_string);
}

template<typename T>
Handle<T> ObjectGet(Handle<Object> object, const char *key) {
  return Handle<T>::Cast(object->Get(NanNew(key)));
}

template<typename T>
Handle<T> ArrayGet(Handle<Array> array, uint32_t i) {
  return Handle<T>::Cast(array->Get(i));
}

rule_ptr RuleFromJsRule(Handle<Object> js_rule) {
  if (!js_rule->IsObject()) {
    NanThrowTypeError("Expected rule to be an object");
    return rule_ptr();
  }

  Handle<String> js_type = ObjectGet<String>(js_rule, "type");
  if (!js_type->IsString()) {
    NanThrowTypeError("Expected rule type to be a string");
    return rule_ptr();
  }

  string type = StringFromJsString(js_type);
  if (type == "BLANK")
    return blank();

  if (type == "CHOICE") {
    Handle<Array> js_members = ObjectGet<Array>(js_rule, "members");
    vector<rule_ptr> members;
    uint32_t length = js_members->Length();
    for (uint32_t i = 0; i < length; i++) {
      Handle<Object> js_member = ArrayGet<Object>(js_members, i);
      rule_ptr member = RuleFromJsRule(js_member);
      if (member.get())
        members.push_back(member);
      else
        return rule_ptr();
    }
    return choice(members);
  }

  if (type == "ERROR")
    return err(RuleFromJsRule(ObjectGet<Object>(js_rule, "value")));

  if (type == "PATTERN")
    return pattern(StringFromJsString(ObjectGet<String>(js_rule, "value")));

  if (type == "REPEAT")
    return repeat(RuleFromJsRule(ObjectGet<Object>(js_rule, "value")));

  if (type == "REPEAT1")
    return repeat1(RuleFromJsRule(ObjectGet<Object>(js_rule, "value")));

  if (type == "SEQ") {
    Handle<Array> js_members = ObjectGet<Array>(js_rule, "members");
    vector<rule_ptr> members;
    uint32_t length = js_members->Length();
    for (uint32_t i = 0; i < length; i++) {
      Handle<Object> js_member = ArrayGet<Object>(js_members, i);
      rule_ptr member = RuleFromJsRule(js_member);
      if (member.get())
        members.push_back(member);
      else
        return rule_ptr();
    }
    return seq(members);
  }

  if (type == "STRING")
    return str(StringFromJsString(ObjectGet<String>(js_rule, "value")));

  if (type == "PREC") {
    rule_ptr rule = RuleFromJsRule(ObjectGet<Object>(js_rule, "rule"));
    if (rule.get())
      return prec(ObjectGet<Integer>(js_rule, "value")->IntegerValue(), rule);
    else
      return rule_ptr();
  }

  if (type == "PREC_LEFT") {
    rule_ptr rule = RuleFromJsRule(ObjectGet<Object>(js_rule, "rule"));
    if (rule.get())
      return prec_left(ObjectGet<Integer>(js_rule, "value")->IntegerValue(), rule);
    else
      return rule_ptr();
  }

  if (type == "PREC_RIGHT") {
    rule_ptr rule = RuleFromJsRule(ObjectGet<Object>(js_rule, "rule"));
    if (rule.get())
      return prec_right(ObjectGet<Integer>(js_rule, "value")->IntegerValue(), rule);
    else
      return rule_ptr();
  }

  if (type == "TOKEN") {
    rule_ptr value = RuleFromJsRule(ObjectGet<Object>(js_rule, "value"));
    if (value.get())
      return token(value);
    else
      return rule_ptr();
  }

  if (type == "SYMBOL")
    return sym(StringFromJsString(ObjectGet<String>(js_rule, "name")));

  NanThrowError((string("Unexpected rule type: ") + type).c_str());
  return rule_ptr();
}

pair<Grammar, bool> GrammarFromJsGrammar(Handle<Object> js_grammar) {
  Handle<Object> js_rules = ObjectGet<Object>(js_grammar, "rules");
  if (!js_rules->IsObject()) {
    NanThrowTypeError("Expected rules to be an object");
    return { Grammar({}), false };
  }

  vector<pair<string, rule_ptr>> rules;
  Local<Array> rule_names = js_rules->GetOwnPropertyNames();
  uint32_t length = rule_names->Length();
  for (uint32_t i = 0; i < length; i++) {
    Local<String> js_rule_name = Local<String>::Cast(rule_names->Get(i));
    string rule_name = StringFromJsString(js_rule_name);
    rule_ptr rule = RuleFromJsRule(Handle<Object>::Cast(js_rules->Get(js_rule_name)));
    if (rule.get()) {
      rules.push_back({ rule_name, rule });
    } else {
      return { Grammar({}), false };
    }
  }

  Grammar result(rules);

  Handle<Array> js_ubiquitous_tokens = ObjectGet<Array>(js_grammar, "ubiquitous");
  if (!js_ubiquitous_tokens->IsUndefined()) {
    if (!js_ubiquitous_tokens->IsArray()) {
      NanThrowTypeError("Expected ubiquitous_tokens to be an array");
      return { Grammar({}), false };
    }

    vector<rule_ptr> ubiquitous_tokens;
    const uint32_t length = js_ubiquitous_tokens->Length();
    for (uint32_t i = 0; i < length; i++)
      ubiquitous_tokens.push_back(RuleFromJsRule(ArrayGet<Object>(js_ubiquitous_tokens, i)));

    result.ubiquitous_tokens(ubiquitous_tokens);
  }

  return { result, true };
}

NAN_METHOD(Compile) {
  NanScope();

  Handle<Object> js_grammar = Handle<Object>::Cast(args[0]);
  if (!js_grammar->IsObject())
    NanThrowTypeError("Expected grammar to be an object");

  Handle<String> js_name = ObjectGet<String>(js_grammar, "name");
  if (!js_name->IsString())
    NanThrowTypeError("Expected grammar name to be a string");

  string name = StringFromJsString(js_name);

  pair<Grammar, bool> grammarResult = GrammarFromJsGrammar(js_grammar);
  if (!grammarResult.second)
    NanReturnUndefined();

  pair<string, const GrammarError *> result = tree_sitter::compile(grammarResult.first, name);
  if (result.second) {
    Local<Value> error = NanError(result.second->message.c_str());
    Local<Object>::Cast(error)->Set(NanNew("isGrammarError"), NanTrue());
    NanThrowError(error);
  }

  NanReturnValue(NanNew(result.first));
}

}  // namespace node_tree_sitter_compiler
