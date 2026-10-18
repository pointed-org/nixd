# Looking up a path *through* a submodule option must not change the option.
# Walking `nested.inner` used to write `nested`'s sub-options back into the
# worker's option tree in place of `nested`, so every later hover on `nested`
# found an attrset with no type or description and rendered "missing type".
#
# Hover `nested.inner` first, then `nested`: the second must still be the option.

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

<-- textDocument/hover(2) on `inner`


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
         "character":14
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
CHECK-NEXT:      "value": " (string)\n\ninside"
```

<-- textDocument/hover(3) on `nested`, after the walk through it


```json
{
   "jsonrpc":"2.0",
   "id":3,
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
     CHECK:  "id": 3,
CHECK-NEXT:  "jsonrpc": "2.0",
CHECK-NEXT:  "result": {
CHECK-NEXT:    "contents": {
CHECK-NEXT:      "kind": "markdown",
CHECK-NEXT:      "value": "submodule (submodule)\n\na bare submodule"
```

```json
{"jsonrpc":"2.0","method":"exit"}
```
