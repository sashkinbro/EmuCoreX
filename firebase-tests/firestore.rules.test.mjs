import { after, before, beforeEach, describe, test } from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import {
  assertFails,
  assertSucceeds,
  initializeTestEnvironment,
} from "@firebase/rules-unit-testing";
import {
  addDoc,
  collection,
  deleteDoc,
  doc,
  getDoc,
  getDocs,
  query,
  orderBy,
  serverTimestamp,
  setDoc,
  where,
  updateDoc,
} from "firebase/firestore";

const projectId = "emucorex-rules-test";
let env;

before(async () => {
  env = await initializeTestEnvironment({
    projectId,
    firestore: { rules: await readFile(new URL("../firestore.rules", import.meta.url), "utf8") },
  });
});

beforeEach(async () => env.clearFirestore());
after(async () => env.cleanup());

const dbFor = (uid, claims = {}) => env.authenticatedContext(uid, claims).firestore();
const guestDb = () => env.unauthenticatedContext().firestore();

async function seed(path, data) {
  await env.withSecurityRulesDisabled(async (context) => {
    await setDoc(doc(context.firestore(), path), data);
  });
}

describe("public website compatibility", () => {
  test("guests can read comments and releases", async () => {
    await seed("games/1/comments/a", { uid: "owner", text: "Works", rating: 5 });
    await seed("releases/stable", { version: "1.2.3" });
    await assertSucceeds(getDoc(doc(guestDb(), "games/1/comments/a")));
    await assertSucceeds(getDoc(doc(guestDb(), "releases/stable")));
  });

  test("signed-in website users can create, edit and delete their own comments", async () => {
    const db = dbFor("owner");
    const ref = doc(db, "games/42/comments/comment-a");
    await assertSucceeds(setDoc(ref, {
      gameId: 42,
      rating: 4,
      text: "Runs well",
      uid: "owner",
      displayName: "Player",
      photoURL: null,
      phoneBrand: "Google",
      phoneId: "device",
      phoneModel: "Pixel",
      phoneName: "Pixel",
      phoneCpu: "Tensor",
      phoneRam: "8 GB",
      createdAt: serverTimestamp(),
    }));
    await assertSucceeds(updateDoc(ref, { rating: 5, text: "Runs perfectly" }));
    await assertSucceeds(deleteDoc(ref));
  });

  test("users cannot edit another user's comment, while the configured admin can delete it", async () => {
    await seed("games/42/comments/comment-a", { uid: "owner", text: "Works", rating: 5 });
    await assertFails(updateDoc(doc(dbFor("other"), "games/42/comments/comment-a"), { text: "Changed" }));
    await assertSucceeds(deleteDoc(doc(
      dbFor("admin", { email: "sashabro1997@gmail.com", email_verified: false }),
      "games/42/comments/comment-a",
    )));
  });

  test("only the configured admin can manage releases", async () => {
    await assertFails(setDoc(doc(dbFor("other"), "releases/stable"), { version: "2" }));
    await assertSucceeds(setDoc(doc(
      dbFor("admin", { email: "sashabro1997@gmail.com", email_verified: false }),
      "releases/stable",
    ), { version: "2" }));
  });
});

describe("profile feature isolation", () => {
  test("devices and emulator profiles are private to their owner", async () => {
    const owner = dbFor("alice");
    await assertSucceeds(setDoc(doc(owner, "users/alice/devices/device-a"), {
      uid: "alice",
      deviceId: "device-a",
      displayName: "Pixel Tablet",
      manufacturer: "Google",
      model: "Pixel Tablet",
      soc: "Tensor",
      gpuFamily: "Mali",
      ramMb: 8192,
      androidVersion: "Android 16",
      appVersion: "1.0",
      coreVersion: "2.7.316",
      isCurrent: true,
      isPublic: false,
      createdAt: serverTimestamp(),
      lastSeenAt: serverTimestamp(),
    }));
    await assertFails(getDoc(doc(dbFor("bob"), "users/alice/devices/device-a")));

    await assertSucceeds(setDoc(doc(owner, "users/alice/emulatorProfiles/profile-a"), {
      uid: "alice",
      profileId: "profile-a",
      name: "Tablet",
      schemaVersion: 1,
      appVersion: "1.0",
      coreVersion: "2.7.316",
      sourceDeviceId: "device-a",
      portableSettings: { renderer: 0 },
      perGameSettings: { profiles: [] },
      createdAt: serverTimestamp(),
      updatedAt: serverTimestamp(),
    }));
    await assertFails(getDoc(doc(dbFor("bob"), "users/alice/emulatorProfiles/profile-a")));
  });

  test("achievement unlocks are immutable and cannot be written for another uid", async () => {
    const alice = dbFor("alice");
    const ref = doc(alice, "achievementUnlocks/alice_first_game");
    await assertSucceeds(setDoc(ref, {
      uid: "alice",
      achievementId: "first_game",
      unlockedAt: serverTimestamp(),
      progress: 1,
      target: 1,
      catalogVersion: 1,
      sourceGameSerial: "SLUS-00000",
      visibility: "public",
    }));
    await assertFails(updateDoc(ref, { progress: 2 }));
    await assertFails(setDoc(doc(alice, "achievementUnlocks/bob_first_game"), {
      uid: "bob",
      achievementId: "first_game",
      unlockedAt: serverTimestamp(),
      progress: 1,
      target: 1,
      catalogVersion: 1,
      sourceGameSerial: "",
      visibility: "public",
    }));
  });

  test("owner can query unlocks and write progress exactly like the Android client", async () => {
    await seed("achievementUnlocks/alice_first_game", {
      uid: "alice", achievementId: "first_game", visibility: "public", unlockedAt: new Date(),
      progress: 1, target: 1, catalogVersion: 1, sourceGameSerial: "",
    });
    const alice = dbFor("alice");
    await assertSucceeds(getDocs(query(
      collection(alice, "achievementUnlocks"),
      where("uid", "==", "alice"),
    )));
    await assertSucceeds(setDoc(doc(alice, "users/alice/achievementProgress/first_game"), {
      achievementId: "first_game", progress: 1, target: 1, updatedAt: serverTimestamp(),
    }));
  });

  test("friend requests can only be accepted by the recipient", async () => {
    const request = {
      id: "alice_bob",
      members: ["alice", "bob"],
      requestedBy: "alice",
      status: "pending",
      createdAt: serverTimestamp(),
      updatedAt: serverTimestamp(),
    };
    await assertSucceeds(setDoc(doc(dbFor("alice"), "friendships/alice_bob"), request));
    await assertFails(updateDoc(doc(dbFor("alice"), "friendships/alice_bob"), {
      status: "accepted",
      updatedAt: serverTimestamp(),
    }));
    await assertSucceeds(updateDoc(doc(dbFor("bob"), "friendships/alice_bob"), {
      status: "accepted",
      updatedAt: serverTimestamp(),
    }));
    await assertSucceeds(getDocs(query(
      collection(dbFor("alice"), "friendships"),
      where("members", "array-contains", "alice"),
      orderBy("updatedAt", "desc"),
    )));
    await assertSucceeds(getDocs(query(
      collection(dbFor("bob"), "friendships"),
      where("members", "array-contains", "bob"),
      orderBy("updatedAt", "desc"),
    )));
    await assertSucceeds(deleteDoc(doc(dbFor("alice"), "friendships/alice_bob")));
  });

  test("blocking either side prevents a friend request until the block is removed", async () => {
    await seed("users/bob/blocks/alice", {
      uid: "bob", blockedUid: "alice", createdAt: new Date(),
    });
    const request = {
      id: "alice_bob",
      members: ["alice", "bob"],
      requestedBy: "alice",
      status: "pending",
      createdAt: serverTimestamp(),
      updatedAt: serverTimestamp(),
    };
    await assertFails(setDoc(doc(dbFor("alice"), "friendships/alice_bob"), request));
    await assertSucceeds(deleteDoc(doc(dbFor("bob"), "users/bob/blocks/alice")));
    await assertSucceeds(setDoc(doc(dbFor("alice"), "friendships/alice_bob"), request));
  });

  test("achievement totals and one chosen device are readable from the public profile", async () => {
    const alice = dbFor("alice");
    await assertSucceeds(setDoc(doc(alice, "publicProfiles/alice"), {
      uid: "alice",
      displayName: "Alice",
      achievementCount: 7,
      achievementPoints: 125,
      achievementCatalogVersion: 1,
      deviceVisibility: true,
      primaryDevice: {
        displayName: "Galaxy",
        soc: "Snapdragon",
        gpuFamily: "Adreno",
        ramMb: 12288,
        androidVersion: "Android 16",
        appVersion: "1.0",
        coreVersion: "2.7.316",
      },
    }, { merge: true }));
    const publicProfile = await assertSucceeds(getDoc(doc(dbFor("bob"), "publicProfiles/alice")));
    assert.equal(publicProfile.data().achievementCount, 7);
    assert.equal(publicProfile.data().primaryDevice.displayName, "Galaxy");
    await assertFails(setDoc(doc(alice, "publicProfiles/alice"), {
      uid: "alice", achievementCount: -1,
    }, { merge: true }));
  });

  test("default rule denies unknown collections", async () => {
    await assertFails(addDoc(collection(dbFor("alice"), "unexpected"), { value: true }));
    assert.ok(true);
  });
});
