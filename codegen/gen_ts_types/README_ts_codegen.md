In this directory, do

```sh
npx tsx src/run.ts
```

TODO: use ByteBuddy instead of trying to parse the unreliable javap. Supposedly
it gives structured metadata output. I don't know enough java to set that up
though. Ideally, we get a json object out of it we can use to generate the
typescript types here.
