#include "story.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zelda_rtl.h"

// The opening legend, retold: Hyrule's wise men hid the Four Sword, a blade
// that splits one hero into four, and four Links once sealed Ganon with it.
// With it, the people of Hyrule speak to four heroes: the lines below replace
// the original messages (their number is the line number in dialogue.txt
// minus one) wherever one talked of a single hero, a boy, a young man.
//
// The [commands] are the game's own text commands. [NextPic] moves the legend
// on to its next picture, so the story keeps the original's picture beats.
// A text box holds three lines of 168 pixels (the font is variable width);
// [2] and [3] start the second and third line, [Scroll] the next one after
// that, and [Waitkey] waits for a key: at most three lines between waits.
// tests/coop/story_text.txt checks every line against the real font.
typedef struct StoryLine {
  uint16 idx;
  const char *text;
} StoryLine;

static const StoryLine kLines[] = {
  // The uncle, in the secret passage
  { 14, "Unnh[...] [Name], I didn't want[2]you in this[...]  Four of you?[3]So the Four Sword has woken[...][Waitkey][Scroll]"
        "Take my sword and shield and[Scroll]listen.  You can focus power in[Scroll]the blade (hold the [B] Button).[Waitkey][Scroll]"
        "[...]Then release it using the[Scroll]secret technique handed down[Scroll]by our people[...][Waitkey][Scroll]"
        "[Name], stand together![Scroll]Save the Princess[...][Scroll]Zelda is your[...] [...] [...]" },
  // The castle soldiers
  { 15, "What're you doing up this late,[2]kids?  You can stay up when[3]you're grown up!  Now go home!" },
  { 17, "You look strong for kids.  How[2]much can you lift?  (Press the[3][A] Button while touching a thing[Waitkey][Scroll]"
        "to lift it.  Two of you can[Scroll]lift even heavier things!)" },
  { 21, "Us soldiers have been around,[2]kids.  You can learn a lot from[3]us.  But you already know that!" },
  // The priest
  { 23, "Princess Zelda, you are safe![2]Is this your doing, [Name]?[3]All four of you, together?" },
  { 27, "[Name]!  You are a second too[2]late.  I have failed[...] Zelda[...][3]The soldiers have[Waitkey][Scroll]"
        "abducted her.[Scroll]They have taken her to[Scroll]the castle.[Waitkey][Scroll]"
        "You must find her before the[Scroll]wizard works his magic.  You[Scroll]four are our only hope[...]" },
  // Zelda, in the sanctuary
  { 39, "[Name], you are wonderful![2]As I thought, the Four Sword[3]has chosen true heroes![Waitkey][Scroll]"
        "Now, you should get the Master[Scroll]Sword.  I am confident that you[Scroll]can defeat the evil wizard!" },
  // The old man in Sahasrahla's house
  { 43, "Who? Oh, it's you, [Name]![2]What can I do for you four?[3]The elder?  Oh, no one[Waitkey][Scroll]"
        "has seen him since the wizard[Scroll]began collecting victims[...][Scroll][...]   [...]   [...][Waitkey][Scroll]"
        "What?  Master Sword?  Well, I[Scroll]don't remember the details[Scroll]exactly, but[...]" },
  // The villager who calls the soldiers
  { 47, "Hey!  Here is [Name], the[2]wanted four!  Soldiers!  Anyone![3]Come quickly!" },
  // Sahasrahla
  { 48, "Incredible!  At last you have[2]the three Pendants[...]  Now, you[3]should go to the Lost Woods.[Waitkey][Scroll]"
        "If you are the true heroes, the[Scroll]sword itself will select you.[Scroll][...] [...] [...]" },
  { 50, "I am, indeed, Sahasrahla, the[2]village elder and a descendent[3]of the seven wise men.[Waitkey][Scroll]"
        "[...]   [...]     Oh really?[Scroll][Name], I am surprised young[Scroll]ones like you are searching for[Waitkey][Scroll]"
        "the sword of evil's bane.[Scroll]Not just anyone can use that[Scroll]weapon.  Legends say only the[Waitkey][Scroll]"
        "Hero who has won the three[Scroll]Pendants can wield the sword.[Scroll][...]   [...]   [...][Waitkey][Scroll]"
        "Do you really want to find it?[Scroll]    > Yeah![Scroll]       Of Course![Choose]" },
  { 54, "[Window 02][Speed 03]But I sense your helplessness[...][2]Before you go any further,[3]find the Moon Pearl on[Waitkey][Scroll]"
        "Death Mountain.  It will protect[Scroll]you from the Dark World's[Scroll]magic so you can keep your[Waitkey][Scroll]"
        "heroic figures." },
  { 56, "Oh!?  You got the Pendant Of[2]Courage!  Now I will tell you[3]more of the legend[...][Waitkey][Scroll]"
        "Three or four generations ago,[Scroll]an order of knights protected[Scroll]the royalty of the Hylia.[Waitkey][Scroll]"
        "These Knights Of Hyrule were[Scroll]also guardians of the Pendant[Scroll]Of Courage.[Waitkey][Scroll]"
        "Unfortunately, most of them[Scroll]were destroyed in the great[Scroll]war against evil that took[Waitkey][Scroll]"
        "place when the seven wise men[Scroll]created their seal.  Among the[Scroll]descendants of the Knights Of[Waitkey][Scroll]"
        "Hyrule a hero must appear, and[Scroll]the Four Sword, it is said,[Scroll]makes that one hero four[...][Waitkey][Scroll]"
        "[...]I see.  [Name], I believe you.[Scroll]You should get the remaining[Scroll]Pendants.[Waitkey][Scroll]"
        "And carry this with you.[Scroll]This is a treasure passed down[Scroll]by the families of the wise[Waitkey][Scroll]"
        "men.  I want you to have it." },
  { 57, "You are correct, young ones![2]I am Sahasrahla, the village[3]elder and a descendent of the[Waitkey][Scroll]"
        "seven wise men.[Scroll][...] [...] [...] What?[Scroll][Name], I'm surprised that[Waitkey][Scroll]"
        "young ones like you seek the[Scroll]sword of evil's bane.  Not just[Scroll]anyone can use that sword.[Waitkey][Scroll]"
        "According to the tales handed[Scroll]down by the Hylia, only the[Scroll]Hero who has destroyed three[Waitkey][Scroll]"
        "great evils and won the three[Scroll]Pendants can wield the sword[...][Scroll][...] [...] [...][Waitkey][Scroll]"
        "I see you have acquired the[Scroll]Pendant of Courage.  I will tell[Scroll]you about the legend behind it.[Waitkey][Scroll]"
        "Three or four generations ago,[Scroll]an order of knights protected[Scroll]the royalty of the Hylia.[Waitkey][Scroll]"
        "These Knights Of Hyrule were[Scroll]also guardians of the Pendant[Scroll]Of Courage.[Waitkey][Scroll]"
        "Unfortunately, most of them[Scroll]were destroyed in the great[Scroll]war against evil that took[Waitkey][Scroll]"
        "place when the seven wise men[Scroll]created their seal.  Among the[Scroll]descendants of the Knights Of[Waitkey][Scroll]"
        "Hyrule a hero must appear, and[Scroll]the Four Sword, it is said,[Scroll]makes that one hero four[...][Waitkey][Scroll]"
        "[...]I see.  [Name], I believe you.[Scroll]You should get the remaining[Scroll]Pendants.[Waitkey][Scroll]"
        "And carry this with you.[Scroll]This is a treasure passed down[Scroll]by the families of the wise[Waitkey][Scroll]"
        "men.  I want you to have it." },
  // Signs in the castle town
  { 58, "I will give 100 Rupees to[2]whoever finds the descendants[3]of the wise men.   THE KING" },
  { 59, "  [LinkL][LinkR]  WANTED!  These are the[2]four who kidnapped Zelda.[3]Call a soldier if you see them!" },
  // The witch's shop
  { 75, "Heh heh[...]  Thank you, young[2]ones[...]  Come back to the shop[3]later for something good[...][Waitkey][Scroll]Heh heh[...]" },
  // The Moon Pearl
  { 94, "You found the Moon Pearl![2]This protects the heroes from[3]the changing effects of the[Waitkey][Scroll]Golden Power." },
  // The old man on Death Mountain
  { 153, "Uhhh[...]  Watch your step.[2]There are holes in the ground.[3]Could you turn right here?[Waitkey][Scroll]"
         "Young ones, are you also going[Scroll]to the mountain to look for the[Scroll]Golden Power?[Waitkey][Scroll]"
         "Just ahead is a mountain full[Scroll]of monsters.  Many people have[Scroll]vanished in this mountain while[Waitkey][Scroll]"
         "looking for the Golden Power.[Scroll][Scroll][Waitkey][Scroll]"
         "I don't want to steer you[Scroll]wrong, so please don't get too[Scroll]involved in such a mad quest." },
  { 157, "The missing maidens are still[2]alive somewhere.  I believe that[3]heroes will rescue them[...][Waitkey][Scroll]"
         "I wait for that day[...][Scroll]Uhh[...]  These are dangerous[Scroll]times[...]  I talked too much.[Waitkey][Scroll]"
         "Anyway[...]  Thank you for your[Scroll]kindness to an old man like me.[Scroll]Uhh[...]  I wanted to give you[Waitkey][Scroll]"
         "this.  If you wander into[Scroll]a magical transporter, gaze[Scroll]into this mirror." },
  // The running man
  { 166, "I can't believe you caught me![2]With your speed it must have[3]been easy to kidnap Zelda[...][Waitkey][Scroll]"
         "You don't look like such a bad[Scroll]bunch, though[...][Scroll]Anyway, because you have[Waitkey][Scroll]"
         "such quick feet, it might be a[Scroll]good idea to run and bash into[Scroll]many things[...][Waitkey][Scroll]"
         "For example, the trees in this[Scroll]village have many useful things[Scroll]hanging in their branches[...][Waitkey][Scroll]"
         "Well, just try it, OK?!" },
  // The castle gate soldier
  { 179, "Hey hey!  You're not allowed in[2]the castle, kids![3]Go home and get some sleep!" },
  // The Master Sword's pedestal
  { 183, "The heroes' triumph on[2]Cataclysm's Eve[3]Wins three symbols of virtue.[Waitkey][Scroll]"
         "The Master Sword they then[Scroll]retrieve,[Scroll]Keeping the Knight's line true." },
  // The bottle vendor
  { 209, "You four!  Have you been going[2]through life without one of my[3]hold-anything bottles?[Waitkey][Scroll]"
         "Well step right up and make[Scroll]your life complete!  I've got[Scroll]one on sale now for the low,[Waitkey][Scroll]"
         "low price of 100 Rupees![Waitkey][Scroll]What do you say?  Interested?[Scroll]    > I'll take one[Scroll]       Don't need it[Choose]" },
  // The smith's lost partner (a frog)
  { 225, "Ribbit ribbit[...]  Your bodies[2]didn't change!  You aren't just[3]ordinary lads, are you?[Waitkey][Scroll]"
         "I used to live in Kakariko Town.[Scroll]I wonder what my partner is[Scroll]doing there without me[...][Waitkey][Scroll]"
         "Ribbit!  I have a request of[Scroll]you.[Scroll]Please take me to my partner![Waitkey][Scroll]"
         "Please!  Ribbit!  Please!" },
  // The fortune teller
  { 239, "[Position 01]Abracadabra alakazam![2]The true heroes will find the[3]Moon Pearl in the[Waitkey][Scroll]mountain tower." },
  { 241, "[Position 01]Abracadabra alakazam![2]The true heroes will jump into[3]the well by the smithy's shop." },
  // The fairy that halves magic
  { 272, "Hey!  Blast you for waking me[2]from my deep, dark sleep![3][...]I mean, thanks a lot, sirs![Waitkey][Scroll]"
         "But now I will get my revenge[Scroll]on you.  Get ready for it![Scroll][...]Err, is that OK with you,[Waitkey][Scroll]"
         "sirs?" },
  // The legend, told over four pictures
  { 0x112, "[ScrollSpd 00][Color 07][Speed 03][Window 02][NextPic]"
           "Long ago, in the kingdom of[2]Hyrule, seven wise men hid[3]the Golden Power away[...][Wait 09][Scroll]"
           "in a secret land. Beside it[Scroll]they laid a blade that could[Scroll]split one hero into four.[Wait 09]"
           "[NextPic][NextPic][Scroll]"
           "Then the thief Ganon broke[Scroll]into the Golden Land[...][Wait 07][Scroll]"
           "and seized its power, and[Scroll]darkness flowed over Hyrule.[Wait 07][Scroll]"
           "So a young knight drew the[Scroll]Four Sword and became four[Scroll]heroes in green, red, blue[Scroll]and purple.[Wait 09]"
           "[NextPic][NextPic][Scroll]"
           "Together they sealed Ganon[Scroll]away for all time[...][Scroll][Wait 09]"
           "[NextPic][NextPic][Scroll]"
           "[...]  [...]But the four heroes[Scroll]faded into the mists of[Scroll]time and became legend[...]" },
  // The throne room
  { 0x113, "[Window 02][Color 07][Speed 03][1]Ages later, a wizard named[2]Agahnim came to Hyrule to[3]break the seal. He struck[Wait 05][Scroll]down the good King[...][Wait 05]" },
  // Zelda's prison
  { 0x114, "[Window 02][Color 07][Speed 03][1]With dark magic, he made the[2]heirs of the wise men and[3]the four knights vanish, one[Wait 05][Scroll]after another.[Wait 05]" },
  // The maiden is taken away
  { 0x115, "[Window 02][Color 07][Speed 03][1]Princess Zelda is next, but[2]the Four Sword stirs again,[3]calling for four heroes[...][Wait 05]" },
  // The bomb shop
  { 279, "100 Rupees for 30 Bombs!  30[2]Bombs for just 100 Rupees![3]Please buy 'em, folks!" },
  { 280, "100 Rupees for 30 Bombs!  I also[2]have a new Super Bomb in stock[3]for only 100 Rupees!  Please[Waitkey][Scroll]"
         "buy it too, folks!" },
  // Aginah, in the desert
  { 293, "I am Aginah.  I sense something[2]is happening in the Golden Land[3]the seven wise men sealed[...][Waitkey][Scroll]"
         "This must be an omen of the[Scroll]Great Cataclysm foretold by[Scroll]the people of Hylian blood[...][Waitkey][Scroll]"
         "[...] [...] [...][Scroll]The prophecy says, \"Heroes[Scroll]will stand in the desert holding[Waitkey][Scroll]"
         "the Book Of Mudora.\"[Scroll]If you have the Book Of Mudora[Scroll]you can read the language of[Waitkey][Scroll]"
         "the Hylia People.[Scroll]It should be in the house of[Scroll]books in the village[...][Waitkey][Scroll]"
         "You must get it![Scroll]If you are the ones who will[Scroll]be the heroes[...]" },
  { 294, "You have collected the three[2]Pendants.[3]If you four are the heroes with[Waitkey][Scroll]"
         "Wisdom, Courage and Power, the[Scroll]Master Sword sleeping in the[Scroll]forest will be yours." },
  { 296, "You are the true heroes[...][2]I believe that you will return[3]peace to this land again." },
  // The maidens
  { 307, "[Speed 02][Position 01][Window 02][Name], because of you, I can[2]escape from the clutches of[3]the evil monsters.  Thank you![Waitkey][Scroll]"
         "[...]The Triforce will grant the[Scroll]wishes of whoever touches it,[Scroll]as long as that person lives[...][Waitkey][Scroll]"
         "That is why it was hidden in[Scroll]the Golden Land.  Only a select[Scroll]few knew of its location,[Waitkey][Scroll]"
         "but at some point that[Scroll]knowledge was lost[...][Scroll]The person who rediscovered[Waitkey][Scroll]"
         "the Golden Land was[Scroll]Ganondorf the evil thief.[Scroll]Luckily, he couldn't figure out[Waitkey][Scroll]"
         "how to return to the Light[Scroll]World[...][Scroll][...]Well, remember that you[Waitkey][Scroll]"
         "have magical powers, which only[Scroll]heroes can make the most of![Scroll]There are some other magical[Waitkey][Scroll]"
         "warping points like the one you[Scroll]saw on Death Mountain.[Scroll]By using them you can go[Waitkey][Scroll]"
         "between the two worlds and[Scroll]find the evils hidden in the[Scroll]Dark World.  You are the only[Waitkey][Scroll]"
         "ones who can destroy[Scroll]Ganondorf, the thief-no,[Scroll]Ganon, the evil King Of[Waitkey][Scroll]"
         "Darkness!" },
  { 308, "[Speed 02][Position 01][Window 02][Name], because of you, I can[2]escape from the clutches of[3]the evil monsters.  Thank you![Waitkey][Scroll]"
         "[...]Do you know the prophecy of[Scroll]the Great Cataclysm?[Scroll]This is the way I heard it[...][Waitkey][Scroll]"
         "If a person who has an evil[Scroll]heart gets the Triforce, a Hero[Scroll]is destined to appear[...][Waitkey][Scroll]"
         "[...]and face the one who began[Scroll]the Great Cataclysm.  The Four[Scroll]Sword makes that Hero four.[Waitkey][Scroll]"
         "If the evil one destroys the[Scroll]heroes, nothing can save the[Scroll]world from his wicked reign.[Waitkey][Scroll]"
         "Only a person of the Knights Of[Scroll]Hyrule, who protected the[Scroll]royalty of Hylia, can become[Waitkey][Scroll]"
         "the Hero[...]  You are of their[Scroll]blood-line, aren't you?  Then[Scroll]you must rescue[Waitkey][Scroll]"
         "Zelda without fail." },
  { 309, "[Speed 02][Position 01][Window 02][Name], because of you, I can[2]escape from the clutches of[3]the evil monsters.  Thank you![Waitkey][Scroll]"
         "As the wise men sealed the way[Scroll]to the Dark World, the Knights[Scroll]Of Hyrule defended them from[Waitkey][Scroll]"
         "the attacks of evil monsters.[Waitkey][Scroll]"
         "I heard that the Knights Of[Scroll]Hyrule were nearly wiped out in[Scroll]that battle[...][Waitkey][Scroll]"
         "You are perhaps the last one[Scroll]to carry on the blood-line of[Scroll]the Knights[...][Waitkey][Scroll]"
         "It is ironic that the last of[Scroll]the line, split in four by the[Scroll]Four Sword, may be the Hero.[Waitkey][Scroll]"
         "Surely you can destroy Ganon!" },
  { 312, "[Speed 02][Position 01][Window 02]I appreciate your coming so far[2]to rescue me.  As I thought,[3]you are the heroes of legend.[Waitkey][Scroll]"
         "I have felt this from the first[Scroll]time we met.[Scroll][...]  [...]  [...][Waitkey][Scroll]"
         "Ganon is waiting inside of his[Scroll]tower to pass through the[Scroll]gate linking the two worlds.[Waitkey][Scroll]"
         "Once Ganon enters the Light[Scroll]World, it is unlikely that[Scroll]anyone can stop him.[Waitkey][Scroll]"
         "But if he stays in the closed[Scroll]space of this world, you can[Scroll]find him wherever he runs.[Waitkey][Scroll]"
         "Now, go to the Tower Of[Scroll]Ganon!  We will use our[Scroll]combined powers to break the[Waitkey][Scroll]"
         "barrier.  Let's return peace to[Scroll]the country without fail[...][Scroll][...]  [...]  [...][Sound 45]" },
  { 313, "[Speed 02][Position 01][Window 02]May the way of the heroes lead[2]to the Triforce." },
  { 316, "[Speed 02][Position 01][Window 02]I appreciate your coming so far[2]to rescue me.  As I thought,[3]you are the heroes of legend.[Waitkey][Scroll]"
         "I have felt this from the first[Scroll]time we met.[Scroll][...]  [...]  [...][Waitkey][Scroll]"
         "Ganon is waiting inside of his[Scroll]tower to pass through the[Scroll]gate linking the two worlds.[Waitkey][Scroll]"
         "Once Ganon enters the Light[Scroll]World, it is unlikely that[Scroll]anyone can stop him.[Waitkey][Scroll]"
         "But if he stays in the closed[Scroll]space of this world, you can[Scroll]find him wherever he runs.[Waitkey][Scroll]"
         "Some maidens still need your[Scroll]help, though.  Once you rescue[Scroll]them all, go to Ganon's Tower.[Waitkey][Scroll]"
         "We who are of the blood-line of[Scroll]the wise men will then use our[Scroll]power to break[Waitkey][Scroll]"
         "Ganon's barrier!  [Scroll][Name][...]  You must return[Scroll]peace to this country[...]" },
  // Agahnim
  { 318, "Ho ho ho[...]  With this, the[2]seal of the seven wise men is[3]at last broken.[Waitkey][Scroll]"
         "It is now only a matter of time[Scroll]before evil power covers this[Scroll]land[...][Waitkey][Scroll]"
         "After all, the heroes of legend[Scroll]cannot defeat us, the tribe of[Scroll]evil, when we are armed with[Waitkey][Scroll]"
         "the Power of Gold.[Scroll]Ho ho ho[...] Now, I must go!" },
  { 320, "Grrrrugh!  Well met!  Like the[2]true heroes that you are[...][3]But I am not ready to admit[Waitkey][Scroll]"
         "defeat yet.  I will draw you[Scroll]into the Dark World!" },
  // King Zora
  { 322, "Wah ha ha!  What do you want,[2]little men?  Do you have[3]something to ask me?[Waitkey][Scroll]"
         "    >  I want the flippers[Scroll]        I just dropped by[Choose]" },
  // The Dark World's changed folk
  { 347, "Oh?  Who are you, bunnies?[2]This world is like the real[3]world, but evil has twisted it.[Waitkey][Scroll]"
         "The Golden Power is what[Scroll]changed your shape to reflect[Scroll]what is in your heart and mind.[Waitkey][Scroll]"
         "I am always changing my mind,[Scroll]so I turned into a ball[...][Scroll]But if you have a ball called[Waitkey][Scroll]"
         "the Moon Pearl, you can keep[Scroll]your original shape here." },
  { 348, "Your shapes didn't change?[2]You aren't just normal guys,[3]are you?" },
  { 349, "What do you want?![2]Do you have something to say[3]to me, silly rabbits?![Waitkey][Scroll]"
         "I came here to get the Power[Scroll]Of Gold but now I'm a freak and[Scroll]I can't go back to the real[Waitkey][Scroll]"
         "world!  If I only had the Moon[Scroll]Pearl from the Tower Of Hera, I[Scroll]could go back to my original[Waitkey][Scroll]"
         "shape!  I've got good reason[Scroll]to be stressed out![Scroll]So back off!  Shoo shoo!" },
  { 350, "WOW!  You kept your shapes![2]You got the Moon Pearl, huh?" },
  // The chest game
  { 354, "Hey kids![2]You can open a chest after[3]paying Rupees!" },
  { 356, "All right, kids.[2]Choose well!  Good luck!" },
  // Ganon
  { 367, "I never imagined boys like you[2]could give me so much trouble.[3]It's unbelievable that you[Waitkey][Scroll]"
         "defeated my alterego, Agahnim[Scroll]the Dark Wizard, twice![Waitkey][Scroll]"
         "But I will never give you the[Scroll]Triforce.  I will destroy you[Scroll]and make my wish to conquer[Waitkey][Scroll]"
         "both Light and Dark Worlds[Scroll]come true without delay." },
  { 368, "You are doing well, lads.  But[2]can you break through this[3]secret technique of Darkness?[Waitkey][Scroll]En Garde!" },
  // The thieves' hideout
  { 369, "Hey kids, this is a secret[2]hideout for a gang of thieves![3]Don't enter without permission![Waitkey][Scroll]"
         "By the way, I heard that one[Scroll]of our ex-members is staying[Scroll]at the entrance to the Desert." },
  // The Triforce
  { 371, "[Speed 02][Position 00][Window 02]Welcome, [Name][...][2]I   am   the   Essence   Of   The[3]Triforce.[Waitkey][Scroll][...]   [...]   [...][Speed 01][Waitkey]"
         "[1]                                          [2]                                          [3]                                          "
         "[Speed 02][1]The Triforce will grant the[2]wishes in the heart and mind of[3]the person who touches it.[Speed 01][Waitkey]"
         "[1]                                          [2]                                          [3]                                          "
         "[Speed 02][1]If a person with a good heart[2]touches it, it will make his good[3]wishes come true[...]  If an evil-[Waitkey][Scroll]"
         "hearted person touches it, it[Scroll]grants his evil wishes.[Speed 01][Waitkey]"
         "[1]                                          [2]                                          [3]                                          "
         "[Speed 02][1]The stronger the wish, the[2]more powerful the Triforce's[3]expression of that wish.[Speed 01][Waitkey]"
         "[1]                                          [2]                                          [3]                                          "
         "[Speed 02][1]Ganon's wish was to conquer[2]the world.  That wish changed[3]the Golden Land to the Dark[Waitkey][Scroll]"
         "World.[Speed 01][Waitkey]"
         "[1]                                          [2]                                          [3]                                          "
         "[Speed 02][1]Ganon was building up his[2]power here so he could conquer[3]the Light World and make his[Waitkey][Scroll]"
         "wish come completely true.[Speed 01][Waitkey]"
         "[1]                                          [2]                                          [3]                                          "
         "[Speed 02][1]But now, four heroes as one[2]destroyed Ganon.  His Dark[3]World will vanish.[Speed 01][Waitkey]"
         "[1]                                          [2]                                          [3]                                          "
         "[Speed 02][1]The Triforce is waiting for a[2]new owner.  Its Golden Power is[3]in your hands[...][Speed 01][Waitkey]"
         "[1]                                          [2]                                          [3]                                          "
         "[Speed 02][1]Now, touch it with one wish in[2]your four hearts.[3][...]  [...]  [...]  [...]" },
};

enum {
  kCommandStart = 0x67,
  kLinePixels = 168,  // 21 tiles of 8 pixels per text box line
  kLinesPerBox = 3,
  kNamePixels = 42,   // what the original text leaves for a [Name]
};

// The US character set (text_compression.py, kTextAlphabet_US).
static const char *const kAlphabet[] = {
  "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P",
  "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "a", "b", "c", "d", "e", "f",
  "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v",
  "w", "x", "y", "z", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "!", "?",
  "-", ".", ",", "[...]", ">", "(", ")", "[Ankh]", "[Waves]", "[Snake]", "[LinkL]", "[LinkR]",
  "\"", "[Up]", "[Down]", "[Left]", "[Right]", "'", "[1HeartL]", "[1HeartR]", "[2HeartL]", "[3HeartL]",
  "[3HeartR]", "[4HeartL]", "[4HeartR]", " ", "<", "[A]", "[B]", "[X]", "[Y]",
};

// Text commands from 0x67 on, and whether they take a parameter byte.
static const struct { const char *name; uint8 has_param; } kCommands[] = {
  {"NextPic", 0}, {"Choose", 0}, {"Item", 0}, {"Name", 0}, {"Window", 1}, {"Number", 1},
  {"Position", 1}, {"ScrollSpd", 1}, {"Selchg", 0}, {"Unused_Crash", 0}, {"Choose3", 0},
  {"Choose2", 0}, {"Scroll", 0}, {"1", 0}, {"2", 0}, {"3", 0}, {"Color", 1},
  {"Wait", 1}, {"Sound", 1}, {"Speed", 1}, {"Unused_Mark", 0}, {"Unused_Mark2", 0},
  {"Unused_Clear", 0}, {"Waitkey", 0},
};
enum { kCmdName = 3, kCmdScroll = 12, kCmd1 = 13, kCmd3 = 15, kCmdWait = 17, kCmdWaitkey = 23 };

static bool g_story_enabled;
static uint8 *g_encoded[countof(kLines)];
static size_t g_encoded_size[countof(kLines)];

void Story_SetEnabled(bool on) {
  g_story_enabled = on;
}

bool Story_Enabled() {
  return g_story_enabled;
}

static size_t Encode(const char *s, uint8 *out, size_t max) {
  size_t n = 0;
  while (*s && n + 2 < max) {
    int best = -1;
    size_t best_len = 0;
    for (int i = 0; i < (int)countof(kAlphabet); i++) {
      size_t l = strlen(kAlphabet[i]);
      if (l > best_len && strncmp(s, kAlphabet[i], l) == 0)
        best = i, best_len = l;
    }
    if (best >= 0) {
      out[n++] = (uint8)best;
      s += best_len;
      continue;
    }
    if (*s == '[') {
      const char *end = strchr(s, ']');
      if (!end)
        break;
      char name[32] = { 0 };
      int param = 0;
      size_t l = end - s - 1;
      memcpy(name, s + 1, l < sizeof(name) - 1 ? l : sizeof(name) - 1);
      char *sp = strchr(name, ' ');
      if (sp) {
        *sp = 0;
        param = atoi(sp + 1);
      }
      for (int i = 0; i < (int)countof(kCommands); i++) {
        if (strcmp(name, kCommands[i].name) == 0) {
          out[n++] = (uint8)(kCommandStart + i);
          if (kCommands[i].has_param)
            out[n++] = (uint8)param;
          break;
        }
      }
      s = end + 1;
      continue;
    }
    s++;  // not in the character set
  }
  return n;
}

static int FindLine(int idx) {
  int lo = 0, hi = (int)countof(kLines) - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (kLines[mid].idx == idx)
      return mid;
    if (kLines[mid].idx < idx)
      lo = mid + 1;
    else
      hi = mid - 1;
  }
  return -1;
}

static void EncodeLine(int i) {
  static uint8 buf[4096];
  if (g_encoded[i])
    return;
  size_t n = Encode(kLines[i].text, buf, sizeof(buf));
  g_encoded[i] = malloc(n ? n : 1);
  memcpy(g_encoded[i], buf, n);
  g_encoded_size[i] = n;
}

bool Story_GetMessage(int idx, const uint8 **data, size_t *size) {
  // Only for the US text encoding (the story is written in English).
  if (!g_story_enabled || (g_zenv.dialogue_flags & 1))
    return false;
  int i = FindLine(idx);
  if (i < 0)
    return false;
  EncodeLine(i);
  *data = g_encoded[i];
  *size = g_encoded_size[i];
  return true;
}

// Measures a message the way the text box draws it: the pixel width of every
// line from the font's width table (a [Name] counts as 42 pixels, the room
// the original text leaves for it), and how many lines come between waits. Returns the number
// of lines that don't fit; the worst line and the longest run of lines go to
// *max_px and *max_lines.
static int MeasureMessage(const uint8 *p, size_t n, int *max_px, int *max_lines, bool verbose, int idx) {
  const uint8 *widths = FindIndexInMemblk(g_zenv.dialogue_font_blk, 1).ptr;
  int px = 0, lines = 1, bad = 0, line_no = 1;
  for (size_t i = 0; i < n; i++) {
    uint8 c = p[i];
    if (c < kCommandStart) {
      px += widths[c];
      continue;
    }
    int cmd = c - kCommandStart;
    if (cmd >= (int)countof(kCommands))
      continue;  // a dictionary word: never emitted here
    if (kCommands[cmd].has_param)
      i++;
    if (cmd == kCmdName) {
      px += kNamePixels;
    } else if (cmd == kCmdScroll || (cmd >= kCmd1 && cmd <= kCmd3)) {
      if (px > kLinePixels) {
        bad++;
        if (verbose)
          printf("  message %d (line %d of dialogue.txt), line %d: %d px, %d too wide\n", idx, idx + 1, line_no, px, px - kLinePixels);
      }
      *max_px = px > *max_px ? px : *max_px;
      px = 0;
      line_no++;
      lines += cmd == kCmdScroll;
      *max_lines = lines > *max_lines ? lines : *max_lines;
    } else if (cmd == kCmdWaitkey || cmd == kCmdWait) {
      lines = 0;
    }
  }
  if (px > kLinePixels) {
    bad++;
    if (verbose)
      printf("  message %d (line %d of dialogue.txt), line %d: %d px, %d too wide\n", idx, idx + 1, line_no, px, px - kLinePixels);
  }
  *max_px = px > *max_px ? px : *max_px;
  return bad;
}

int Story_CheckText(bool verbose) {
  int bad = 0, max_px = 0, max_lines = 0;
  for (int i = 0; i < (int)countof(kLines); i++) {
    EncodeLine(i);
    int lines = 0, px = 0;
    bad += MeasureMessage(g_encoded[i], g_encoded_size[i], &px, &lines, verbose, kLines[i].idx);
    if (lines > kLinesPerBox + 1) {  // (the first page is three lines after the [1] line)
      bad++;
      if (verbose)
        printf("  message %d (line %d of dialogue.txt): %d lines between key waits\n", kLines[i].idx, kLines[i].idx + 1, lines);
    }
    max_px = px > max_px ? px : max_px;
    max_lines = lines > max_lines ? lines : max_lines;
    if (i > 0 && kLines[i].idx <= kLines[i - 1].idx) {
      bad++;
      if (verbose)
        printf("  message %d is out of order in kLines\n", kLines[i].idx);
    }
  }
  if (verbose)
    printf("story text: %d messages, widest line %d of %d px, at most %d lines between key waits\n",
           (int)countof(kLines), max_px, kLinePixels, max_lines);
  return bad;
}

int Story_MeasureOriginals(bool verbose) {
  MemBlk dialogue = FindIndexInMemblk(g_zenv.dialogue_blk, 1);
  MemBlk dictionary = FindIndexInMemblk(g_zenv.dialogue_blk, 0);
  int max_px = 0, max_lines = 0, bad = 0;
  for (int idx = 0; idx < 397; idx++) {
    MemBlk m = FindIndexInMemblk(dialogue, idx);
    // Expand the dictionary words like Text_LoadCharacterBuffer does.
    static uint8 buf[4096];
    size_t n = 0;
    for (size_t j = 0; j < m.size && n + 64 < sizeof(buf); j++) {
      uint8 c = m.ptr[j];
      if (c >= 0x88) {
        MemBlk w = FindIndexInMemblk(dictionary, c - 0x88);
        memcpy(buf + n, w.ptr, w.size);
        n += w.size;
      } else {
        buf[n++] = c;
      }
    }
    int px = 0, lines = 0;
    bad += MeasureMessage(buf, n, &px, &lines, verbose, idx);
    max_px = px > max_px ? px : max_px;
    max_lines = lines > max_lines ? lines : max_lines;
  }
  if (verbose)
    printf("original text: widest line %d px, at most %d lines between key waits, %d over\n", max_px, max_lines, bad);
  return max_px;
}
