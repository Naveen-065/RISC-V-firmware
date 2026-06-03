//             **** The Main TMR Design (START) ****
module zes400_voter_bit (
  input  wire A,
  input  wire B,
  input  wire C,
  output wire Y,
  output wire E
  );

  assign Y = (A & B) | (B & C) | (A & C);
  assign E = (A ^ B) | (B ^ C);

endmodule
//             **** The Main TMR Design (END) ****


//////////////////////////////////////////////////////////////////////////////


//             **** How to use in top wrappers (START) ****
zes400_voter_bit ack_voter (
  .A(ack0),
  .B(ack1),
  .C(ack2),
  .Y(ack_voted),
  .E(ack_error)
 );
//             **** How to use in top wrappers (END) ****
 