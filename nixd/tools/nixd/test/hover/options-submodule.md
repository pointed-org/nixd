# An option whose type is a bare `submodule` is the option itself on hover, not
# the attrset of its sub-options. Regression test for the case where mangling
# ran unconditionally and left hover with no type, description or declaration.
#
# `nested` below is that option; `wrapped` is the `attrsOf submodule` case that
# already worked and must keep working.

# RUN: nixd --lit-test \
# RUN: --nixos-options-expr="{ nested = { _type = \"option\"; description = \"a bare submodule\"; type = { _type = \"option-type\"; name = \"submodule\"; description = \"submodule\"; getSubOptions = _: { inner = { _type = \"option\"; description = \"inside\"; type.description = \"string\"; }; }; }; }; }" \
# RUN: < %s | FileCheck %s


<-- initialize(0)

```json
{
   "jsonrpc":"2.0",
   "id":0,
   "method":"initialize",
   "params":{
      "processId":123,
      "rootPath":"",
      "capabilities":{
      },
      "trace":"off"
   }
}
```


<-- textDocument/didOpen

```nix file:///submodule.nix
{ nested = { inner = "x"; }; }
```

<-- textDocument/hover(2)


```json
{
   "jsonrpc":"2.0",
   "id":2,
   "method":"textDocument/hover",
   "params":{
      "textDocument":{
         "uri":"file:///submodule.nix"
      },
      "position":{
         "line":0,
         "character":3
      }
   }
}
```

```
     CHECK:  "id": 2,
CHECK-NEXT:  "jsonrpc": "2.0",
CHECK-NEXT:  "result": {
CHECK-NEXT:    "contents": {
CHECK-NEXT:      "kind": "markdown",
CHECK-NEXT:      "value": " (submodule)\n\na bare submodule"
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
